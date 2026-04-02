/**
 * @file https_test.cc
 * @brief Test HTTPS integration (HTTP + TLS via StreamUpgrader)
 */
#include <nitrocoro/io/Stream.h>
#include <nitrocoro/testing/Test.h>
#include <nitrocoro/utils/Format.h>

#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/https/HttpsClient.h>
#include <nitrocoro/tls/TlsContext.h>
#include <nitrocoro/tls/TlsPolicy.h>
#include <nitrocoro/tls/TlsStream.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

using namespace nitrocoro;
using namespace nitrocoro::http;
using namespace nitrocoro::https;
using namespace nitrocoro::tls;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static std::pair<std::string, std::string> makeTestCert(const std::string & cn)
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
                               reinterpret_cast<const unsigned char *>(cn.c_str()), -1, -1, 0);
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

static TlsContextPtr makeServerCtx(const std::string & cn = "localhost")
{
    auto [certPem, keyPem] = makeTestCert(cn);
    TlsPolicy sp;
    sp.certPem = certPem;
    sp.keyPem = keyPem;
    sp.validate = false;
    return TlsContext::createServer(sp);
}

static HttpsClientConfig noVerifyConfig()
{
    HttpsClientConfig config;
    config.tlsPolicy.validate = false;
    return config;
}

static HttpServer makeHttpsServer()
{
    auto serverCtx = makeServerCtx();
    HttpServer server(0);
    server.setStreamUpgrader([serverCtx](net::TcpConnectionPtr conn) -> Task<io::StreamPtr> {
        auto tlsStream = co_await TlsStream::accept(conn, serverCtx);
        if (!tlsStream)
            co_return nullptr;
        co_return std::make_shared<io::Stream>(tlsStream);
    });
    return server;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

NITRO_TEST(https_get)
{
    auto server = makeHttpsServer();

    server.route("/", { "GET" }, [](auto req, auto resp) {
        resp->setBody("Hello, HTTPS!");
    });

    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    HttpsClient client(utils::format("https://127.0.0.1:{}", server.listeningPort()), noVerifyConfig());
    auto response = co_await client.get("/");
    NITRO_CHECK_EQ(response.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(response.body(), "Hello, HTTPS!");

    co_await server.stop();
}

NITRO_TEST(https_post)
{
    auto server = makeHttpsServer();

    server.route("/echo", { "POST" }, [](auto req, auto resp) -> Task<> {
        utils::StringBuffer buffer;
        co_await req->readToEnd(buffer);
        resp->setBody(buffer.extract());
    });

    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    HttpsClient client(utils::format("https://127.0.0.1:{}", server.listeningPort()), noVerifyConfig());
    std::string testData = "Test POST data over HTTPS";
    auto response = co_await client.post("/echo", testData);
    NITRO_CHECK_EQ(response.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(response.body(), testData);

    co_await server.stop();
}

NITRO_TEST(https_large_body)
{
    auto server = makeHttpsServer();

    constexpr size_t largeSize = 128 * 1024;
    std::string largeBody(largeSize, 'x');

    server.route("/large", { "GET" }, [largeBody](auto req, auto resp) {
        resp->setBody(largeBody);
    });

    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    HttpsClient client(utils::format("https://127.0.0.1:{}", server.listeningPort()), noVerifyConfig());
    auto response = co_await client.get("/large");
    NITRO_CHECK_EQ(response.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(response.body().size(), largeSize);
    NITRO_CHECK_EQ(response.body(), largeBody);

    co_await server.stop();
}

NITRO_TEST(https_chunked)
{
    auto server = makeHttpsServer();

    server.route("/chunked", { "GET" }, [](auto req, auto resp) {
        resp->setBody([](auto & writer) -> Task<> {
            co_await writer.write("chunk1");
            co_await writer.write("chunk2");
            co_await writer.write("chunk3");
        });
    });

    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    HttpsClient client(utils::format("https://127.0.0.1:{}", server.listeningPort()), noVerifyConfig());
    auto response = co_await client.get("/chunked");
    NITRO_CHECK_EQ(response.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(response.body(), "chunk1chunk2chunk3");

    co_await server.stop();
}

NITRO_TEST(https_multiple_requests)
{
    auto server = makeHttpsServer();

    server.route("/1", { "GET" }, [](auto req, auto resp) { resp->setBody("Response 1"); });
    server.route("/2", { "GET" }, [](auto req, auto resp) { resp->setBody("Response 2"); });
    server.route("/3", { "GET" }, [](auto req, auto resp) { resp->setBody("Response 3"); });

    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    HttpsClient client(utils::format("https://127.0.0.1:{}", server.listeningPort()), noVerifyConfig());
    auto r1 = co_await client.get("/1");
    NITRO_CHECK_EQ(r1.body(), "Response 1");

    auto r2 = co_await client.get("/2");
    NITRO_CHECK_EQ(r2.body(), "Response 2");

    auto r3 = co_await client.get("/3");
    NITRO_CHECK_EQ(r3.body(), "Response 3");

    co_await server.stop();
}

NITRO_TEST(http_without_upgrader)
{
    HttpServer server(0);

    server.route("/", { "GET" }, [](auto req, auto resp) {
        resp->setBody("Plain HTTP");
    });

    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    auto response = co_await http::get(utils::format("http://127.0.0.1:{}/", server.listeningPort()));
    NITRO_CHECK_EQ(response.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(response.body(), "Plain HTTP");

    co_await server.stop();
}

NITRO_TEST(https_sni_hostname)
{
    auto serverCtx = makeServerCtx("localhost");
    std::string capturedSni;
    HttpServer server(0);
    server.setStreamUpgrader([serverCtx, &capturedSni](net::TcpConnectionPtr conn) -> Task<io::StreamPtr> {
        auto tlsStream = co_await TlsStream::accept(conn, serverCtx);
        if (!tlsStream)
            co_return nullptr;
        capturedSni = tlsStream->sniName();
        co_return std::make_shared<io::Stream>(tlsStream);
    });
    server.route("/", { "GET" }, [](auto req, auto resp) { resp->setBody("ok"); });
    Scheduler::current()->spawn([&]() -> Task<> { co_await server.start(); });
    co_await server.started();

    HttpsClient client(utils::format("https://127.0.0.1:{}", server.listeningPort()), noVerifyConfig());

    auto response = co_await client.get("/");
    NITRO_CHECK_EQ(response.statusCode(), StatusCode::k200OK);
    NITRO_CHECK_EQ(capturedSni, "127.0.0.1");

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
