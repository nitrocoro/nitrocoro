/**
 * @file http2_test.cc
 * @brief HTTP/2 protocol-level tests using a raw frame client.
 *
 * Tests here require direct control over HTTP/2 frames (multiplexing,
 * interleaved DATA, ALPN fallback) that cannot be expressed via Http2Client.
 * Both h2c (plain TCP) and h2s (TLS) transports are covered.
 */
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/http2/Http2Server.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/testing/Test.h>
#include <nitrocoro/tls/TlsContext.h>
#include <nitrocoro/tls/TlsPolicy.h>
#include <nitrocoro/tls/TlsStream.h>

#include <atomic>
#include <optional>
#include <unordered_map>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

using namespace nitrocoro;
using namespace nitrocoro::http;
using namespace nitrocoro::http2;
using namespace nitrocoro::tls;
using namespace nitrocoro::net;
using namespace std::chrono_literals;

// ── Cert helper ───────────────────────────────────────────────────────────────

static std::pair<std::string, std::string> makeTestCert()
{
    EVP_PKEY_CTX * kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    EVP_PKEY_keygen_init(kctx);
    EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kctx, NID_X9_62_prime256v1);
    EVP_PKEY * pkey = nullptr;
    EVP_PKEY_keygen(kctx, &pkey);
    EVP_PKEY_CTX_free(kctx);

    X509 * x509 = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 365 * 24 * 3600);
    X509_set_pubkey(x509, pkey);
    X509_NAME * name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char *>("localhost"), -1, -1, 0);
    X509_set_issuer_name(x509, name);
    X509_sign(x509, pkey, EVP_sha256());

    auto bioToStr = [](BIO * bio) {
        BUF_MEM * mem;
        BIO_get_mem_ptr(bio, &mem);
        std::string s(mem->data, mem->length);
        BIO_free(bio);
        return s;
    };

    BIO * certBio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(certBio, x509);
    std::string certPem = bioToStr(certBio);

    BIO * keyBio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(keyBio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    std::string keyPem = bioToStr(keyBio);

    X509_free(x509);
    EVP_PKEY_free(pkey);
    return { certPem, keyPem };
}

// ── Raw frame helpers ─────────────────────────────────────────────────────────

// Generic stream abstraction so RawH2Client works over both TCP and TLS.
struct IStream
{
    virtual Task<size_t> read(void * buf, size_t len) = 0;
    virtual Task<size_t> write(const void * buf, size_t len) = 0;
    virtual ~IStream() = default;
};

struct TcpStream : IStream
{
    explicit TcpStream(TcpConnectionPtr c)
        : conn(std::move(c)) {}
    Task<size_t> read(void * buf, size_t len) override { co_return co_await conn->read(buf, len); }
    Task<size_t> write(const void * buf, size_t len) override { co_return co_await conn->write(buf, len); }
    TcpConnectionPtr conn;
};

struct TlsStreamWrapper : IStream
{
    explicit TlsStreamWrapper(TlsStreamPtr t)
        : tls(std::move(t)) {}
    Task<size_t> read(void * buf, size_t len) override { co_return co_await tls->read(buf, len); }
    Task<size_t> write(const void * buf, size_t len) override { co_return co_await tls->write(buf, len); }
    TlsStreamPtr tls;
};

static Task<> writeExact(IStream & s, const void * buf, size_t len)
{
    size_t sent = 0;
    const auto * p = static_cast<const uint8_t *>(buf);
    while (sent < len)
    {
        size_t n = co_await s.write(p + sent, len - sent);
        if (n == 0)
            throw std::runtime_error("connection closed during write");
        sent += n;
    }
}

static Task<> readExact(IStream & s, void * buf, size_t len)
{
    size_t got = 0;
    auto * p = static_cast<uint8_t *>(buf);
    while (got < len)
    {
        size_t n = co_await s.read(p + got, len - got);
        if (n == 0)
            throw std::runtime_error("connection closed during read");
        got += n;
    }
}

struct RawFrameHeader
{
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t streamId;
};

static Task<RawFrameHeader> readFrameHeader(IStream & s)
{
    uint8_t hdr[9];
    co_await readExact(s, hdr, 9);
    RawFrameHeader f{};
    f.length = (static_cast<uint32_t>(hdr[0]) << 16)
               | (static_cast<uint32_t>(hdr[1]) << 8) | hdr[2];
    f.type = hdr[3];
    f.flags = hdr[4];
    f.streamId = ((static_cast<uint32_t>(hdr[5]) & 0x7f) << 24)
                 | (static_cast<uint32_t>(hdr[6]) << 16)
                 | (static_cast<uint32_t>(hdr[7]) << 8) | hdr[8];
    co_return f;
}

static Task<std::vector<uint8_t>> readFramePayload(IStream & s, uint32_t len)
{
    std::vector<uint8_t> buf(len);
    if (len > 0)
        co_await readExact(s, buf.data(), len);
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
    if (payload)
        out.insert(out.end(), static_cast<const uint8_t *>(payload),
                   static_cast<const uint8_t *>(payload) + payLen);
}

static void hpackLiteral(std::vector<uint8_t> & out, std::string_view name, std::string_view value)
{
    out.push_back(0x00);
    out.push_back(static_cast<uint8_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    out.push_back(static_cast<uint8_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

static std::vector<uint8_t> makeGetFrame(uint32_t streamId, std::string_view path,
                                         std::string_view authority)
{
    std::vector<uint8_t> block;
    block.push_back(0x82); // :method GET
    if (path == "/")
        block.push_back(0x84); // :path /
    else
        hpackLiteral(block, ":path", path);
    block.push_back(0x86); // :scheme http
    hpackLiteral(block, ":authority", authority);

    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x1, 0x5 /*END_HEADERS|END_STREAM*/, streamId,
                  block.data(), block.size());
    return buf;
}

static std::vector<uint8_t> makePostHeaders(uint32_t streamId, std::string_view path,
                                            std::string_view authority)
{
    std::vector<uint8_t> block;
    block.push_back(0x83); // :method POST
    hpackLiteral(block, ":path", path);
    block.push_back(0x86); // :scheme http
    hpackLiteral(block, ":authority", authority);

    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x1, 0x4 /*END_HEADERS*/, streamId, block.data(), block.size());
    return buf;
}

static std::vector<uint8_t> makeDataFrame(uint32_t streamId, std::string_view data, bool endStream)
{
    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x0, endStream ? 0x1 : 0x0, streamId, data.data(), data.size());
    return buf;
}

// ── RawH2Client ──────────────────────────────────────────────────────────────────

struct StreamResponse
{
    std::vector<uint8_t> headerBlock;
    std::string body;
};

class RawH2Client
{
public:
    explicit RawH2Client(std::shared_ptr<IStream> stream)
        : stream_(std::move(stream))
    {
        Scheduler::current()->spawn([this]() -> Task<> { co_await pump(); });
    }

    Task<> send(std::vector<uint8_t> frame)
    {
        co_await writeExact(*stream_, frame.data(), frame.size());
    }

    Task<StreamResponse> recv(uint32_t streamId)
    {
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
            RawFrameHeader fh{};
            try
            {
                fh = co_await readFrameHeader(*stream_);
            }
            catch (...)
            {
                break;
            }
            auto payload = co_await readFramePayload(*stream_, fh.length);

            if (fh.type == 0x4 && !(fh.flags & 0x1)) // SETTINGS (not ACK)
            {
                buf.clear();
                writeFrameRaw(buf, 0x4, 0x1, 0, nullptr, 0);
                co_await writeExact(*stream_, buf.data(), buf.size());
                continue;
            }

            if (fh.streamId == 0)
                continue;

            auto & entry = streams_[fh.streamId];
            if (!entry.promise)
                entry.promise.emplace(Scheduler::current());

            if (fh.type == 0x1)
                entry.response.headerBlock = std::move(payload);
            if (fh.type == 0x0)
                entry.response.body.append(
                    reinterpret_cast<const char *>(payload.data()), payload.size());

            if (fh.flags & 0x1) // END_STREAM
                entry.promise->set_value();
        }
    }

    std::shared_ptr<IStream> stream_;
    std::unordered_map<uint32_t, StreamEntry> streams_;
};

static Task<std::shared_ptr<RawH2Client>> h2connect(uint16_t port)
{
    auto conn = co_await TcpConnection::connect(InetAddress("127.0.0.1", port));
    auto stream = std::make_shared<TcpStream>(std::move(conn));

    std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    co_await writeExact(*stream, preface.data(), preface.size());

    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x4, 0, 0, nullptr, 0);
    co_await writeExact(*stream, buf.data(), buf.size());

    auto sh = co_await readFrameHeader(*stream);
    co_await readFramePayload(*stream, sh.length);
    buf.clear();
    writeFrameRaw(buf, 0x4, 0x1, 0, nullptr, 0);
    co_await writeExact(*stream, buf.data(), buf.size());

    co_return std::make_shared<RawH2Client>(std::move(stream));
}

static Task<std::shared_ptr<RawH2Client>> h2sConnect(uint16_t port, TlsContextPtr clientCtx)
{
    auto conn = co_await TcpConnection::connect(InetAddress("127.0.0.1", port));
    auto tls = co_await TlsStream::connect(conn, clientCtx);
    auto stream = std::make_shared<TlsStreamWrapper>(std::move(tls));

    std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    co_await writeExact(*stream, preface.data(), preface.size());

    std::vector<uint8_t> buf;
    writeFrameRaw(buf, 0x4, 0, 0, nullptr, 0);
    co_await writeExact(*stream, buf.data(), buf.size());

    auto sh = co_await readFrameHeader(*stream);
    co_await readFramePayload(*stream, sh.length);
    buf.clear();
    writeFrameRaw(buf, 0x4, 0x1, 0, nullptr, 0);
    co_await writeExact(*stream, buf.data(), buf.size());

    co_return std::make_shared<RawH2Client>(std::move(stream));
}

static TlsPolicy makeServerPolicy()
{
    auto [certPem, keyPem] = makeTestCert();
    TlsPolicy sp;
    sp.certPem = certPem;
    sp.keyPem = keyPem;
    sp.validate = false;
    return sp;
}

static TlsContextPtr makeClientCtx(std::vector<std::string> alpn)
{
    TlsPolicy cp;
    cp.hostname = "localhost";
    cp.validate = false;
    cp.alpn = std::move(alpn);
    return TlsContext::createClient(cp);
}

// ── h2c: multiplexing ─────────────────────────────────────────────────────────

NITRO_TEST(h2c_multiple_streams)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/a", { "GET" }, [](auto req, auto resp) { resp->setBody("aaa"); });
    server.route("/b", { "GET" }, [](auto req, auto resp) { resp->setBody("bbb"); });
    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    auto port = server.listeningPort();
    std::string host = "127.0.0.1:" + std::to_string(port);
    auto client = co_await h2connect(port);

    co_await client->send(makeGetFrame(1, "/a", host));
    co_await client->send(makeGetFrame(3, "/b", host));

    auto r1 = co_await client->recv(1);
    auto r3 = co_await client->recv(3);

    NITRO_CHECK_EQ(r1.body, "aaa");
    NITRO_CHECK_EQ(r3.body, "bbb");

    co_await server.stop();
}

// Two GET streams send response DATA frames strictly alternating.
// Stream 1 writes on even turns, stream 3 on odd turns.
NITRO_TEST(h2c_interleaved_frames)
{
    auto counter = std::make_shared<std::atomic_int>(0);

    HttpServer server(0);
    enableHttp2(server);
    server.route("/s1", { "GET" }, [counter](auto req, auto resp) -> Task<> {
        resp->setBody([counter](http::BodyWriter & w) -> Task<> {
            for (int i = 0; i < 5; ++i)
            {
                while (counter->load() != i * 2)
                    co_await sleep(1ms);
                co_await w.write("A");
                counter->fetch_add(1);
            }
        });
        co_return;
    });
    server.route("/s3", { "GET" }, [counter](auto req, auto resp) -> Task<> {
        resp->setBody([counter](http::BodyWriter & w) -> Task<> {
            for (int i = 0; i < 5; ++i)
            {
                while (counter->load() != i * 2 + 1)
                    co_await sleep(1ms);
                co_await w.write("B");
                counter->fetch_add(1);
            }
        });
        co_return;
    });
    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    auto port = server.listeningPort();
    std::string host = "127.0.0.1:" + std::to_string(port);
    auto client = co_await h2connect(port);

    co_await client->send(makeGetFrame(1, "/s1", host));
    co_await client->send(makeGetFrame(3, "/s3", host));

    auto r1 = co_await client->recv(1);
    auto r3 = co_await client->recv(3);

    NITRO_CHECK_EQ(r1.body, "AAAAA");
    NITRO_CHECK_EQ(r3.body, "BBBBB");

    co_await server.stop();
}

// Two POST streams send DATA frames interleaved; server reads body streaming.
NITRO_TEST(h2c_post_streaming_interleaved)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/echo", { "POST" }, [](IncomingRequestPtr req, ServerResponsePtr resp) -> Task<> {
        char buf[64];
        std::string result;
        size_t n;
        while ((n = co_await req->read(buf, sizeof(buf))) > 0)
            result.append(buf, n);
        resp->setBody(result);
    });
    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    auto port = server.listeningPort();
    std::string host = "127.0.0.1:" + std::to_string(port);
    auto client = co_await h2connect(port);

    co_await client->send(makePostHeaders(1, "/echo", host));
    co_await client->send(makePostHeaders(3, "/echo", host));

    co_await client->send(makeDataFrame(1, "foo", false));
    co_await client->send(makeDataFrame(3, "bar", false));
    co_await client->send(makeDataFrame(1, "baz", false));
    co_await client->send(makeDataFrame(3, "qux", false));
    co_await client->send(makeDataFrame(1, "!", true));
    co_await client->send(makeDataFrame(3, "!", true));

    auto r1 = co_await client->recv(1);
    auto r3 = co_await client->recv(3);

    NITRO_CHECK_EQ(r1.body, "foobaz!");
    NITRO_CHECK_EQ(r3.body, "barqux!");

    co_await server.stop();
}

// ── h2s: multiplexing ─────────────────────────────────────────────────────────

NITRO_TEST(h2s_multiple_streams)
{
    auto serverPolicy = makeServerPolicy();
    auto clientCtx = makeClientCtx({ "h2" });

    HttpServer server(0);
    enableHttp2(server, serverPolicy);
    server.route("/a", { "GET" }, [](auto req, auto resp) { resp->setBody("aaa"); });
    server.route("/b", { "GET" }, [](auto req, auto resp) { resp->setBody("bbb"); });
    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    auto port = server.listeningPort();
    std::string host = "127.0.0.1:" + std::to_string(port);
    auto client = co_await h2sConnect(port, clientCtx);

    co_await client->send(makeGetFrame(1, "/a", host));
    co_await client->send(makeGetFrame(3, "/b", host));

    auto r1 = co_await client->recv(1);
    auto r3 = co_await client->recv(3);

    NITRO_CHECK_EQ(r1.body, "aaa");
    NITRO_CHECK_EQ(r3.body, "bbb");

    co_await server.stop();
}

// ── h2s: ALPN fallback ────────────────────────────────────────────────────────

// Client advertises only "http/1.1" → server falls back, HTTP/1.1 request succeeds.
NITRO_TEST(h2s_fallback_http1)
{
    auto serverPolicy = makeServerPolicy();
    auto clientCtx = makeClientCtx({ "http/1.1" });

    HttpServer server(0);
    enableHttp2(server, serverPolicy);
    server.route("/", { "GET" }, [](auto req, auto resp) {});
    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    auto conn = co_await TcpConnection::connect({ "127.0.0.1", server.listeningPort() });
    auto tls = co_await TlsStream::connect(conn, clientCtx);
    NITRO_CHECK_EQ(tls->negotiatedAlpn(), std::string("http/1.1"));

    std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    co_await tls->write(req.data(), req.size());

    std::string resp;
    char buf[4096];
    size_t n;
    while ((n = co_await tls->read(buf, sizeof(buf))) > 0)
        resp.append(buf, n);

    NITRO_CHECK(resp.find("HTTP/1.1 200") != std::string::npos);

    co_await server.stop();
}

// Client advertises "h2" → h2 is selected, not fallback.
NITRO_TEST(h2s_fallback_h2_preferred)
{
    auto serverPolicy = makeServerPolicy();
    auto clientCtx = makeClientCtx({ "h2" });

    HttpServer server(0);
    enableHttp2(server, serverPolicy);
    server.route("/", { "GET" }, [](auto req, auto resp) { resp->setBody("h2ok"); });
    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    auto port = server.listeningPort();
    std::string host = "127.0.0.1:" + std::to_string(port);
    auto client = co_await h2sConnect(port, clientCtx);
    co_await client->send(makeGetFrame(1, "/", host));
    auto r = co_await client->recv(1);

    NITRO_CHECK_EQ(r.body, "h2ok");

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
