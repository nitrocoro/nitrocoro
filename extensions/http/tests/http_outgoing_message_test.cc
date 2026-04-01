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

    NITRO_CHECK(hasHeader(mock->data, "content-length", "5"));
    NITRO_CHECK(!hasHeader(mock->data, "transfer-encoding"));
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

    NITRO_CHECK(hasHeader(mock->data, "transfer-encoding", "chunked"));
    NITRO_CHECK(!hasHeader(mock->data, "content-length"));
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

    NITRO_CHECK(hasHeader(mock->data, "connection", "close"));
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

    NITRO_CHECK(!hasHeader(mock->data, "transfer-encoding"));
    NITRO_CHECK(hasHeader(mock->data, "connection", "close"));
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

    NITRO_CHECK(hasHeader(mock->data, "content-length", "2"));
    NITRO_CHECK(!hasHeader(mock->data, "content-length: 999"));
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
    NITRO_CHECK(hasHeader(mock->data, "content-length", "4"));
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

    NITRO_CHECK(hasHeader(mock->data, "transfer-encoding", "chunked"));
    NITRO_CHECK(!hasHeader(mock->data, "content-length"));
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
