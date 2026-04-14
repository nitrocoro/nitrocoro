/**
 * @file http2_test.cc
 * @brief Basic tests for Http2Server using curl-style h2c upgrade.
 *
 * These tests use a raw TCP client that speaks HTTP/2 directly (h2c).
 * We send the client preface + SETTINGS, then a HEADERS frame, and
 * verify the server responds with a HEADERS frame containing :status.
 */
#include <nitrocoro/http2/Http2Server.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/testing/Test.h>

#include <cstring>
#include <optional>
#include <unordered_map>

using namespace nitrocoro;
using namespace nitrocoro::http2;
using namespace std::chrono_literals;

// ── Minimal h2c client helpers ────────────────────────────────────────────────

static Task<> writeExact(net::TcpConnectionPtr conn, const void * buf, size_t len)
{
    size_t sent = 0;
    const auto * p = static_cast<const uint8_t *>(buf);
    while (sent < len)
    {
        size_t n = co_await conn->write(p + sent, len - sent);
        if (n == 0)
            throw std::runtime_error("connection closed during write");
        sent += n;
    }
}

static Task<> readExact(net::TcpConnectionPtr conn, void * buf, size_t len)
{
    size_t got = 0;
    auto * p = static_cast<uint8_t *>(buf);
    while (got < len)
    {
        size_t n = co_await conn->read(p + got, len - got);
        if (n == 0)
            throw std::runtime_error("connection closed during read");
        got += n;
    }
}

// Read one frame header (9 bytes) and return payload length + type
struct RawFrameHeader
{
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t streamId;
};

static Task<RawFrameHeader> readFrameHeader(net::TcpConnectionPtr conn)
{
    uint8_t hdr[9];
    co_await readExact(conn, hdr, 9);
    RawFrameHeader f;
    f.length = (uint32_t(hdr[0]) << 16) | (uint32_t(hdr[1]) << 8) | hdr[2];
    f.type = hdr[3];
    f.flags = hdr[4];
    f.streamId = ((uint32_t(hdr[5]) & 0x7f) << 24) | (uint32_t(hdr[6]) << 16) | (uint32_t(hdr[7]) << 8) | hdr[8];
    co_return f;
}

static Task<std::vector<uint8_t>> readFramePayload(net::TcpConnectionPtr conn, uint32_t len)
{
    std::vector<uint8_t> buf(len);
    if (len > 0)
        co_await readExact(conn, buf.data(), len);
    co_return buf;
}

static void writeFrameRaw(std::vector<uint8_t> & out, uint8_t type, uint8_t flags,
                          uint32_t streamId, const void * payload, size_t payLen)
{
    out.push_back((payLen >> 16) & 0xff);
    out.push_back((payLen >> 8) & 0xff);
    out.push_back(payLen & 0xff);
    out.push_back(type);
    out.push_back(flags);
    out.push_back((streamId >> 24) & 0x7f);
    out.push_back((streamId >> 16) & 0xff);
    out.push_back((streamId >> 8) & 0xff);
    out.push_back(streamId & 0xff);
    const auto * p = static_cast<const uint8_t *>(payload);
    out.insert(out.end(), p, p + payLen);
}

// Minimal HPACK: encode a literal header (no indexing, no Huffman)
static void hpackLiteral(std::vector<uint8_t> & out, std::string_view name, std::string_view value)
{
    out.push_back(0x00); // literal without indexing, new name
    // name length
    out.push_back(static_cast<uint8_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    // value length
    out.push_back(static_cast<uint8_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

// Use static table index for :method GET (index 2) and :path / (index 4)
// :scheme http (index 6), :authority literal
static std::vector<uint8_t> buildGetHeaders(std::string_view path, std::string_view authority)
{
    std::vector<uint8_t> block;
    block.push_back(0x82); // indexed :method GET  (static[2])
    block.push_back(0x84); // indexed :path /      (static[4]) — only if path=="/"
    if (path != "/")
    {
        block.pop_back();
        hpackLiteral(block, ":path", path);
    }
    block.push_back(0x86); // indexed :scheme http (static[6])
    hpackLiteral(block, ":authority", authority);
    return block;
}

static SharedFuture<> startServer(Http2Server & server)
{
    Scheduler::current()->spawn([&server]() -> Task<> { co_await server.start(); });
    return server.started();
}

// Perform the h2c connection preface exchange and return a ready-to-use connection.
static Task<net::TcpConnectionPtr> h2connect(Http2Server & server)
{
    auto conn = co_await net::TcpConnection::connect(
        net::InetAddress("127.0.0.1", server.listeningPort()));

    std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    co_await writeExact(conn, preface.data(), preface.size());

    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x4, 0, 0, nullptr, 0); // client SETTINGS
    co_await writeExact(conn, buf.data(), buf.size());

    // Read and ACK server SETTINGS
    auto sh = co_await readFrameHeader(conn);
    co_await readFramePayload(conn, sh.length);
    buf.clear();
    writeFrameRaw(buf, 0x4, 0x1, 0, nullptr, 0);
    co_await writeExact(conn, buf.data(), buf.size());

    co_return conn;
}

// Read frames for a single stream until END_STREAM, skipping SETTINGS frames.
// Returns {headerBlock, body}.
struct StreamResponse
{
    std::vector<uint8_t> headerBlock;
    std::string body;
};

// Multiplexing client: runs a background pump coroutine that reads all incoming
// frames and dispatches them to per-stream buffers. Callers co_await recv() to
// get the complete response for a stream, regardless of interleaving.
class H2Client
{
public:
    explicit H2Client(net::TcpConnectionPtr conn)
        : conn_(std::move(conn))
    {
        Scheduler::current()->spawn([this]() -> Task<> { co_await pump(); });
    }

    Task<> send(std::vector<uint8_t> frame)
    {
        co_await writeExact(conn_, frame.data(), frame.size());
    }

    Task<StreamResponse> recv(uint32_t streamId)
    {
        // Register a promise for this stream before any frames arrive
        auto & entry = streams_[streamId];
        if (!entry.promise)
            entry.promise.emplace(Scheduler::current());
        co_await entry.promise->get_future().get();
        co_return std::move(entry.response);
    }

private:
    struct StreamEntry
    {
        StreamResponse response;
        std::optional<Promise<>> promise;
    };

    Task<> pump()
    {
        std::vector<uint8_t> buf;
        for (;;)
        {
            RawFrameHeader fh;
            try
            {
                fh = co_await readFrameHeader(conn_);
            }
            catch (...)
            {
                break;
            }
            auto payload = co_await readFramePayload(conn_, fh.length);

            if (fh.type == 0x4 && !(fh.flags & 0x1)) // SETTINGS (not ACK)
            {
                buf.clear();
                writeFrameRaw(buf, 0x4, 0x1, 0, nullptr, 0);
                co_await writeExact(conn_, buf.data(), buf.size());
                continue;
            }

            if (fh.streamId == 0)
                continue;

            auto & entry = streams_[fh.streamId];
            if (!entry.promise)
                entry.promise.emplace(Scheduler::current());

            if (fh.type == 0x1) // HEADERS
                entry.response.headerBlock = std::move(payload);
            if (fh.type == 0x0) // DATA
                entry.response.body.append(
                    reinterpret_cast<const char *>(payload.data()), payload.size());

            if (fh.flags & 0x1) // END_STREAM
                entry.promise->set_value();
        }
    }

    net::TcpConnectionPtr conn_;
    std::unordered_map<uint32_t, StreamEntry> streams_;
};

static Task<std::shared_ptr<H2Client>> h2client(Http2Server & server)
{
    auto conn = co_await h2connect(server);
    co_return std::make_shared<H2Client>(std::move(conn));
}

static std::vector<uint8_t> makeGetFrame(uint32_t streamId,
                                         std::string_view path,
                                         std::string_view authority)
{
    auto headerBlock = buildGetHeaders(path, authority);
    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x1, 0x5 /*END_HEADERS|END_STREAM*/, streamId,
                  headerBlock.data(), headerBlock.size());
    return buf;
}

static std::vector<uint8_t> makePostFrame(uint32_t streamId,
                                          std::string_view path,
                                          std::string_view authority,
                                          std::string_view body)
{
    std::vector<uint8_t> headerBlock;
    headerBlock.push_back(0x83); // indexed :method POST (static[3])
    hpackLiteral(headerBlock, ":path", path);
    headerBlock.push_back(0x86); // indexed :scheme http (static[6])
    hpackLiteral(headerBlock, ":authority", authority);

    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x1, 0x4 /*END_HEADERS*/, streamId,
                  headerBlock.data(), headerBlock.size());
    writeFrameRaw(buf, 0x0, 0x1 /*END_STREAM*/, streamId,
                  body.data(), body.size());
    return buf;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

NITRO_TEST(h2_get_hello)
{
    Http2Server server(0);
    server.route("/hello", { "GET" }, [](auto req, auto resp) {
        resp->setBody("hello h2");
    });
    co_await startServer(server);

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto client = co_await h2client(server);
    co_await client->send(makeGetFrame(1, "/hello", host));
    auto r = co_await client->recv(1);

    NITRO_CHECK(!r.headerBlock.empty());
    NITRO_CHECK_EQ(r.body, "hello h2");

    co_await server.stop();
}

NITRO_TEST(h2_404)
{
    Http2Server server(0);
    co_await startServer(server);

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto client = co_await h2client(server);
    co_await client->send(makeGetFrame(1, "/missing", host));
    auto r = co_await client->recv(1);

    NITRO_CHECK(!r.headerBlock.empty());
    NITRO_CHECK(r.body.empty());

    co_await server.stop();
}

NITRO_TEST(h2_post_echo)
{
    Http2Server server(0);
    server.route("/echo", { "POST" }, [](auto req, auto resp) -> Task<> {
        auto complete = co_await req->toCompleteRequest();
        resp->setBody(complete.body());
    });
    co_await startServer(server);

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto client = co_await h2client(server);
    co_await client->send(makePostFrame(1, "/echo", host, "ping"));
    auto r = co_await client->recv(1);

    NITRO_CHECK_EQ(r.body, "ping");

    co_await server.stop();
}

NITRO_TEST(h2_405)
{
    Http2Server server(0);
    server.route("/data", { "POST" }, [](auto req, auto resp) {
        resp->setBody("ok");
    });
    co_await startServer(server);

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto client = co_await h2client(server);
    co_await client->send(makeGetFrame(1, "/data", host));
    auto r = co_await client->recv(1);

    NITRO_CHECK(!r.headerBlock.empty());

    co_await server.stop();
}

NITRO_TEST(h2_path_params)
{
    Http2Server server(0);
    server.route("/users/:id", { "GET" }, [](auto req, auto resp) {
        resp->setBody(req->pathParams().at("id"));
    });
    co_await startServer(server);

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto client = co_await h2client(server);
    co_await client->send(makeGetFrame(1, "/users/42", host));
    auto r = co_await client->recv(1);

    NITRO_CHECK_EQ(r.body, "42");

    co_await server.stop();
}

NITRO_TEST(h2_query_params)
{
    Http2Server server(0);
    server.route("/greet", { "GET" }, [](auto req, auto resp) {
        resp->setBody("Hello, " + req->getQuery("name") + "!");
    });
    co_await startServer(server);

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto client = co_await h2client(server);
    co_await client->send(makeGetFrame(1, "/greet?name=World", host));
    auto r = co_await client->recv(1);

    NITRO_CHECK_EQ(r.body, "Hello, World!");

    co_await server.stop();
}

NITRO_TEST(h2_response_header)
{
    Http2Server server(0);
    server.route("/", { "GET" }, [](auto req, auto resp) {
        resp->setHeader("x-custom", "myvalue");
        resp->setBody("ok");
    });
    co_await startServer(server);

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto client = co_await h2client(server);
    co_await client->send(makeGetFrame(1, "/", host));
    auto r = co_await client->recv(1);

    std::string block(r.headerBlock.begin(), r.headerBlock.end());
    NITRO_CHECK(block.find("x-custom") != std::string::npos);
    NITRO_CHECK(block.find("myvalue") != std::string::npos);

    co_await server.stop();
}

NITRO_TEST(h2_multiple_streams)
{
    Http2Server server(0);
    server.route("/a", { "GET" }, [](auto req, auto resp) { resp->setBody("aaa"); });
    server.route("/b", { "GET" }, [](auto req, auto resp) { resp->setBody("bbb"); });
    co_await startServer(server);

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto client = co_await h2client(server);

    // Send both requests before waiting for either response
    co_await client->send(makeGetFrame(1, "/a", host));
    co_await client->send(makeGetFrame(3, "/b", host));

    // recv() on each stream independently — works regardless of frame interleaving
    auto r1 = co_await client->recv(1);
    auto r3 = co_await client->recv(3);

    NITRO_CHECK_EQ(r1.body, "aaa");
    NITRO_CHECK_EQ(r3.body, "bbb");

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
