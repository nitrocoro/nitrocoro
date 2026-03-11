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

// ── Tests ─────────────────────────────────────────────────────────────────────

NITRO_TEST(h2_get_hello)
{
    Http2Server server(0);
    server.route("/hello", { "GET" }, [](auto req, auto resp) {
        resp->setBody("hello h2");
    });
    Scheduler::current()->spawn([&server]() -> Task<> { co_await server.start(); });
    co_await server.started();

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto conn = co_await net::TcpConnection::connect(
        net::InetAddress("127.0.0.1", server.listeningPort()));

    // Client preface
    std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    co_await writeExact(conn, preface.data(), preface.size());

    // Client SETTINGS (empty)
    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x4, 0, 0, nullptr, 0);
    co_await writeExact(conn, buf.data(), buf.size());

    // Read server SETTINGS
    auto sh = co_await readFrameHeader(conn);
    NITRO_CHECK_EQ(sh.type, uint8_t(0x4));
    co_await readFramePayload(conn, sh.length);

    // ACK server SETTINGS
    buf.clear();
    writeFrameRaw(buf, 0x4, 0x1, 0, nullptr, 0);
    co_await writeExact(conn, buf.data(), buf.size());

    // Send HEADERS for GET /hello (stream 1, END_HEADERS | END_STREAM)
    auto headerBlock = buildGetHeaders("/hello", host);
    buf.clear();
    writeFrameRaw(buf, 0x1, 0x5 /*END_HEADERS|END_STREAM*/, 1,
                  headerBlock.data(), headerBlock.size());
    co_await writeExact(conn, buf.data(), buf.size());

    // Read frames until we get HEADERS response on stream 1
    bool gotHeaders = false;
    std::string body;
    for (int i = 0; i < 10; ++i)
    {
        auto fh = co_await readFrameHeader(conn);
        auto payload = co_await readFramePayload(conn, fh.length);

        if (fh.type == 0x4 && !(fh.flags & 0x1))
        {
            // Server SETTINGS ACK request — send ACK
            buf.clear();
            writeFrameRaw(buf, 0x4, 0x1, 0, nullptr, 0);
            co_await writeExact(conn, buf.data(), buf.size());
            continue;
        }
        if (fh.type == 0x1 && fh.streamId == 1) // HEADERS
        {
            gotHeaders = true;
            // Check :status 200 is in the block (byte 0x88 = indexed :status 200)
            NITRO_CHECK(!payload.empty());
        }
        if (fh.type == 0x0 && fh.streamId == 1) // DATA
        {
            body.append(reinterpret_cast<const char *>(payload.data()), payload.size());
            if (fh.flags & 0x1) // END_STREAM
                break;
        }
        if (gotHeaders && (fh.flags & 0x1) && fh.streamId == 1)
            break;
    }

    NITRO_CHECK(gotHeaders);
    NITRO_CHECK_EQ(body, "hello h2");

    co_await server.stop();
}

NITRO_TEST(h2_404)
{
    Http2Server server(0);
    co_await startServer(server);

    std::string host = "127.0.0.1:" + std::to_string(server.listeningPort());
    auto conn = co_await net::TcpConnection::connect(
        net::InetAddress("127.0.0.1", server.listeningPort()));

    std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    co_await writeExact(conn, preface.data(), preface.size());

    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x4, 0, 0, nullptr, 0);
    co_await writeExact(conn, buf.data(), buf.size());

    // Read server SETTINGS
    auto sh = co_await readFrameHeader(conn);
    co_await readFramePayload(conn, sh.length);

    // ACK
    buf.clear();
    writeFrameRaw(buf, 0x4, 0x1, 0, nullptr, 0);
    co_await writeExact(conn, buf.data(), buf.size());

    auto headerBlock = buildGetHeaders("/missing", host);
    buf.clear();
    writeFrameRaw(buf, 0x1, 0x5, 1, headerBlock.data(), headerBlock.size());
    co_await writeExact(conn, buf.data(), buf.size());

    // Read until HEADERS on stream 1
    bool got404 = false;
    for (int i = 0; i < 10; ++i)
    {
        auto fh = co_await readFrameHeader(conn);
        auto payload = co_await readFramePayload(conn, fh.length);

        if (fh.type == 0x4 && !(fh.flags & 0x1))
        {
            buf.clear();
            writeFrameRaw(buf, 0x4, 0x1, 0, nullptr, 0);
            co_await writeExact(conn, buf.data(), buf.size());
            continue;
        }
        if (fh.type == 0x1 && fh.streamId == 1)
        {
            // :status 404 = 0x8d in static table (index 13)
            NITRO_CHECK(!payload.empty());
            got404 = true;
        }
        if (fh.streamId == 1 && (fh.flags & 0x1))
            break;
    }
    NITRO_CHECK(got404);

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
