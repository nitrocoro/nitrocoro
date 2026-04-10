/**
 * @file http_outgoing_message_test.cc
 * @brief Unit tests for HttpOutgoingMessage serialization via flush()
 */
#include <nitrocoro/http/HttpStream.h>
#include <nitrocoro/testing/Test.h>

using namespace nitrocoro;
using namespace nitrocoro::http;

// ── MockStream ────────────────────────────────────────────────────────────────

struct MockStream
{
    std::string data;

    Task<size_t> read(void *, size_t) { co_return 0; }
    Task<size_t> write(const void * buf, size_t len)
    {
        data.append(static_cast<const char *>(buf), len);
        co_return len;
    }
    Task<> shutdown() { co_return; }
};

static std::pair<std::shared_ptr<MockStream>, io::StreamPtr> makeStream()
{
    auto mock = std::make_shared<MockStream>();
    auto stream = std::make_shared<io::Stream>(mock);
    return { mock, stream };
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool hasHeader(const std::string & raw, std::string_view name, std::string_view value)
{
    std::string needle = std::string(name) + ": " + std::string(value) + "\r\n";
    // case-insensitive name match not needed here — we use lowercase names from HttpHeader
    return raw.find(needle) != std::string::npos;
}

static bool hasHeader(const std::string & raw, std::string_view name)
{
    return raw.find(std::string(name) + ": ") != std::string::npos;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

/** setBody(string) produces Content-Length header with correct value and body appended. */
NITRO_TEST(outgoing_response_content_length)
{
    auto [mock, stream] = makeStream();
    ServerResponse resp;
    resp.setStatus(StatusCode::k200OK);
    resp.setBody("hello");
    co_await resp.flush(stream);

    NITRO_CHECK(hasHeader(mock->data, "Content-Length", "5"));
    NITRO_CHECK(!hasHeader(mock->data, "Transfer-Encoding"));
    NITRO_CHECK(mock->data.ends_with("hello"));
}

/** setBody(fn) produces Transfer-Encoding: chunked and no Content-Length. */
NITRO_TEST(outgoing_response_chunked)
{
    auto [mock, stream] = makeStream();
    ServerResponse resp;
    resp.setStatus(StatusCode::k200OK);
    resp.setBody([](auto & w) -> Task<> { co_await w.write("chunk"); });
    co_await resp.flush(stream);

    NITRO_CHECK(hasHeader(mock->data, "Transfer-Encoding", "chunked"));
    NITRO_CHECK(!hasHeader(mock->data, "Content-Length"));
}

/** shouldClose produces Connection: close header. */
NITRO_TEST(outgoing_response_connection_close)
{
    auto [mock, stream] = makeStream();
    ServerResponse resp;
    resp.setStatus(StatusCode::k200OK);
    resp.setCloseConnection(true);
    resp.setBody("ok");
    co_await resp.flush(stream);

    NITRO_CHECK(hasHeader(mock->data, "Connection", "close"));
}

/** HTTP/1.0 + setBody(fn) falls back to UntilClose: no chunked header, Connection: close. */
NITRO_TEST(outgoing_response_http10_streaming_fallback)
{
    auto [mock, stream] = makeStream();
    ServerResponse resp;
    resp.setVersion(Version::kHttp10);
    resp.setStatus(StatusCode::k200OK);
    resp.setBody([](auto & w) -> Task<> { co_await w.write("data"); });
    co_await resp.flush(stream);

    NITRO_CHECK(!hasHeader(mock->data, "Transfer-Encoding"));
    NITRO_CHECK(!hasHeader(mock->data, "Connection"));
    NITRO_CHECK(mock->data.find("HTTP/1.0") != std::string::npos);
}

/** User-set Content-Length is overridden by actual body size. */
NITRO_TEST(outgoing_response_content_length_override)
{
    auto [mock, stream] = makeStream();
    ServerResponse resp;
    resp.setStatus(StatusCode::k200OK);
    resp.setHeader(HttpHeader::NameCode::ContentLength, "999");
    resp.setBody("hi");
    co_await resp.flush(stream);

    NITRO_CHECK(hasHeader(mock->data, "Content-Length", "2"));
    NITRO_CHECK(!hasHeader(mock->data, "Content-Length: 999"));
}

/** ClientRequest flush produces correct request line and Content-Length. */
NITRO_TEST(outgoing_request_content_length)
{
    auto [mock, stream] = makeStream();
    ClientRequest req;
    req.setMethod(methods::Post);
    req.setPath("/echo");
    req.setBody("ping");
    co_await req.flush(stream);

    NITRO_CHECK(mock->data.find("POST /echo HTTP/1.1\r\n") != std::string::npos);
    NITRO_CHECK(hasHeader(mock->data, "Content-Length", "4"));
    NITRO_CHECK(mock->data.ends_with("ping"));
}

/** ClientRequest with setBody(fn) produces Transfer-Encoding: chunked. */
NITRO_TEST(outgoing_request_chunked)
{
    auto [mock, stream] = makeStream();
    ClientRequest req;
    req.setMethod(methods::Post);
    req.setPath("/upload");
    req.setBody([](auto & w) -> Task<> { co_await w.write("abc"); });
    co_await req.flush(stream);

    NITRO_CHECK(hasHeader(mock->data, "Transfer-Encoding", "chunked"));
    NITRO_CHECK(!hasHeader(mock->data, "Content-Length"));
}

/** clear() resets headers and body; closeConnection_ is preserved. */
NITRO_TEST(outgoing_response_clear)
{
    auto [mock, stream] = makeStream();
    ServerResponse resp;
    resp.setStatus(StatusCode::k200OK);
    resp.setHeader("X-Custom", "value");
    resp.setCloseConnection(true);
    resp.clear();
    resp.setStatus(StatusCode::k500InternalServerError);
    resp.setBody("error");
    co_await resp.flush(stream);

    NITRO_CHECK(!hasHeader(mock->data, "X-Custom"));
    NITRO_CHECK(hasHeader(mock->data, "Connection", "close"));
    NITRO_CHECK(mock->data.find("500") != std::string::npos);
}

/** HTTP/1.0 response default produces Connection: keep-alive. */
NITRO_TEST(outgoing_response_http10_keep_alive)
{
    auto [mock, stream] = makeStream();
    ServerResponse resp;
    resp.setVersion(Version::kHttp10);
    resp.setStatus(StatusCode::k200OK);
    resp.setBody("ok");
    co_await resp.flush(stream);

    NITRO_CHECK(hasHeader(mock->data, "Connection", "keep-alive"));
}

/** HTTP/1.0 response with closeConnection produces no Connection header. */
NITRO_TEST(outgoing_response_http10_close)
{
    auto [mock, stream] = makeStream();
    ServerResponse resp;
    resp.setVersion(Version::kHttp10);
    resp.setStatus(StatusCode::k200OK);
    resp.setCloseConnection(true);
    resp.setBody("ok");
    co_await resp.flush(stream);

    NITRO_CHECK(!hasHeader(mock->data, "Connection"));
}

/** HTTP/1.0 streaming body overrides user-set Connection header with close. */
NITRO_TEST(outgoing_response_http10_streaming_override_connection)
{
    auto [mock, stream] = makeStream();
    ServerResponse resp;
    resp.setVersion(Version::kHttp10);
    resp.setStatus(StatusCode::k200OK);
    resp.setHeader(HttpHeader::NameCode::Connection, "keep-alive");
    resp.setBody([](auto & w) -> Task<> { co_await w.write("data"); });
    co_await resp.flush(stream);

    NITRO_CHECK(!hasHeader(mock->data, "Connection", "keep-alive"));
}

/** HTTP/1.0 request with setKeepAlive(true) produces Connection: keep-alive. */
NITRO_TEST(outgoing_request_http10_keep_alive)
{
    auto [mock, stream] = makeStream();
    ClientRequest req;
    req.setVersion(Version::kHttp10);
    req.setMethod(methods::Get);
    req.setPath("/");
    req.setKeepAlive(true);
    co_await req.flush(stream);

    NITRO_CHECK(hasHeader(mock->data, "Connection", "keep-alive"));
}

/** HTTP/1.1 request with setKeepAlive(false) produces Connection: close. */
NITRO_TEST(outgoing_request_connection_close)
{
    auto [mock, stream] = makeStream();
    ClientRequest req;
    req.setMethod(methods::Get);
    req.setPath("/");
    req.setKeepAlive(false);
    co_await req.flush(stream);

    NITRO_CHECK(hasHeader(mock->data, "Connection", "close"));
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
