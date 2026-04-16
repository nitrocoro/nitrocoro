/**
 * @file http2_client_test.cc
 * @brief Http2Client API tests against a local Http2Server.
 *
 * Covers all business-level scenarios (routing, status codes, headers, body)
 * for both h2c (plain TCP) and https (TLS + ALPN) transports.
 */
#include <nitrocoro/http/HttpServer.h>
#include <nitrocoro/http/Cookie.h>
#include <nitrocoro/http/CookieStore.h>
#include <nitrocoro/http2/Http2Client.h>
#include <nitrocoro/http2/Http2Server.h>
#include <nitrocoro/testing/Test.h>
#include <nitrocoro/tls/TlsContext.h>
#include <nitrocoro/tls/TlsPolicy.h>
#include <nitrocoro/tls/TlsStream.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

using namespace nitrocoro;
using namespace nitrocoro::http;
using namespace nitrocoro::http2;
using namespace nitrocoro::tls;

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

// ── Shared server setup helpers ───────────────────────────────────────────────

static void startServer(HttpServer & server)
{
    Scheduler::current()->spawn([&server]() -> Task<> { co_await server.start(); });
}

// Returns a base URL for h2c.
static std::string h2cUrl(uint16_t port)
{
    return "http://127.0.0.1:" + std::to_string(port);
}

// Returns a configured Http2Client for https with cert validation disabled.
static Http2Client makeHttpsClient(uint16_t port)
{
    Http2ClientConfig config;
    config.tls_policy.hostname = "localhost";
    config.tls_policy.validate = false;
    return Http2Client("https://localhost:" + std::to_string(port), config);
}

// ── h2c tests ─────────────────────────────────────────────────────────────────

NITRO_TEST(h2c_get)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/hello", { "GET" }, [](auto req, auto resp) {
        resp->setBody("hello h2c");
    });
    startServer(server);
    co_await server.started();

    Http2Client client(h2cUrl(server.listeningPort()));
    auto resp = co_await client.get("/hello");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.body(), "hello h2c");

    co_await server.stop();
}

NITRO_TEST(h2c_404)
{
    HttpServer server(0);
    enableHttp2(server);
    startServer(server);
    co_await server.started();

    Http2Client client(h2cUrl(server.listeningPort()));
    auto resp = co_await client.get("/missing");

    NITRO_CHECK_EQ(resp.statusCode(), 404);

    co_await server.stop();
}

NITRO_TEST(h2c_post_echo)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/echo", { "POST" }, [](auto req, auto resp) -> Task<> {
        auto complete = co_await req->toCompleteRequest();
        resp->setBody(complete.body());
    });
    startServer(server);
    co_await server.started();

    Http2Client client(h2cUrl(server.listeningPort()));
    auto resp = co_await client.post("/echo", "ping");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.body(), "ping");

    co_await server.stop();
}

NITRO_TEST(h2c_405)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/data", { "POST" }, [](auto req, auto resp) { resp->setBody("ok"); });
    startServer(server);
    co_await server.started();

    Http2Client client(h2cUrl(server.listeningPort()));
    auto resp = co_await client.get("/data");

    NITRO_CHECK_EQ(resp.statusCode(), 405);

    co_await server.stop();
}

NITRO_TEST(h2c_path_params)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/users/:id", { "GET" }, [](auto req, auto resp) {
        resp->setBody(req->pathParams().at("id"));
    });
    startServer(server);
    co_await server.started();

    Http2Client client(h2cUrl(server.listeningPort()));
    auto resp = co_await client.get("/users/42");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.body(), "42");

    co_await server.stop();
}

NITRO_TEST(h2c_query_params)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/greet", { "GET" }, [](auto req, auto resp) {
        resp->setBody("Hello, " + req->getQuery("name") + "!");
    });
    startServer(server);
    co_await server.started();

    Http2Client client(h2cUrl(server.listeningPort()));
    auto resp = co_await client.get("/greet?name=World");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.body(), "Hello, World!");

    co_await server.stop();
}

NITRO_TEST(h2c_response_header)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/", { "GET" }, [](auto req, auto resp) {
        resp->setHeader("x-custom", "myvalue");
        resp->setBody("ok");
    });
    startServer(server);
    co_await server.started();

    Http2Client client(h2cUrl(server.listeningPort()));
    auto resp = co_await client.get("/");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.getHeader("x-custom"), "myvalue");

    co_await server.stop();
}

// ── https tests ───────────────────────────────────────────────────────────────

NITRO_TEST(https_get)
{
    auto [certPem, keyPem] = makeTestCert();
    TlsPolicy serverPolicy;
    serverPolicy.certPem = certPem;
    serverPolicy.keyPem = keyPem;
    serverPolicy.validate = false;

    HttpServer server(0);
    enableHttp2(server, serverPolicy);
    server.route("/hello", { "GET" }, [](auto req, auto resp) {
        resp->setBody("hello https");
    });
    startServer(server);
    co_await server.started();

    auto client = makeHttpsClient(server.listeningPort());
    auto resp = co_await client.get("/hello");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.body(), "hello https");

    co_await server.stop();
}

NITRO_TEST(https_404)
{
    auto [certPem, keyPem] = makeTestCert();
    TlsPolicy serverPolicy;
    serverPolicy.certPem = certPem;
    serverPolicy.keyPem = keyPem;
    serverPolicy.validate = false;

    HttpServer server(0);
    enableHttp2(server, serverPolicy);
    startServer(server);
    co_await server.started();

    auto client = makeHttpsClient(server.listeningPort());
    auto resp = co_await client.get("/missing");

    NITRO_CHECK_EQ(resp.statusCode(), 404);

    co_await server.stop();
}

NITRO_TEST(https_post_echo)
{
    auto [certPem, keyPem] = makeTestCert();
    TlsPolicy serverPolicy;
    serverPolicy.certPem = certPem;
    serverPolicy.keyPem = keyPem;
    serverPolicy.validate = false;

    HttpServer server(0);
    enableHttp2(server, serverPolicy);
    server.route("/echo", { "POST" }, [](auto req, auto resp) -> Task<> {
        auto complete = co_await req->toCompleteRequest();
        resp->setBody(complete.body());
    });
    startServer(server);
    co_await server.started();

    auto client = makeHttpsClient(server.listeningPort());

    ClientRequest req;
    req.setMethod(methods::Post);
    req.setPath("/echo");
    req.setBody("hello body");
    auto inResp = co_await client.request(std::move(req));
    auto complete = co_await inResp.toCompleteResponse();

    NITRO_CHECK_EQ(complete.statusCode(), 200);
    NITRO_CHECK_EQ(complete.body(), "hello body");

    co_await server.stop();
}

NITRO_TEST(https_path_params)
{
    auto [certPem, keyPem] = makeTestCert();
    TlsPolicy serverPolicy;
    serverPolicy.certPem = certPem;
    serverPolicy.keyPem = keyPem;
    serverPolicy.validate = false;

    HttpServer server(0);
    enableHttp2(server, serverPolicy);
    server.route("/users/:id", { "GET" }, [](auto req, auto resp) {
        resp->setBody(req->pathParams().at("id"));
    });
    startServer(server);
    co_await server.started();

    auto client = makeHttpsClient(server.listeningPort());
    auto resp = co_await client.get("/users/99");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.body(), "99");

    co_await server.stop();
}

// ── Cookie tests ────────────────────────────────────────────────────────────────────

NITRO_TEST(h2c_server_set_cookie)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/set", { "GET" }, [](auto req, auto resp) {
        resp->addCookie(Cookie{ .name = "session", .value = "abc123", .path = "/" });
        resp->setBody("ok");
    });
    startServer(server);
    co_await server.started();

    Http2Client client(h2cUrl(server.listeningPort()));
    auto resp = co_await client.get("/set");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_REQUIRE_EQ(resp.cookies().size(), 1);
    NITRO_CHECK_EQ(resp.cookies()[0].name, "session");
    NITRO_CHECK_EQ(resp.cookies()[0].value, "abc123");

    co_await server.stop();
}

NITRO_TEST(h2c_cookie_store_and_send)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/set", { "GET" }, [](auto req, auto resp) {
        resp->addCookie(Cookie{ .name = "session", .value = "abc123", .path = "/" });
        resp->setBody("ok");
    });
    server.route("/check", { "GET" }, [](auto req, auto resp) {
        resp->setBody(req->getCookie("session"));
    });
    startServer(server);
    co_await server.started();

    Http2ClientConfig config;
    config.cookieStoreFactory = memoryCookieStore();
    Http2Client client(h2cUrl(server.listeningPort()), std::move(config));

    co_await client.get("/set");
    auto resp = co_await client.get("/check");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.body(), "abc123");

    co_await server.stop();
}

NITRO_TEST(h2c_no_cookie_store)
{
    HttpServer server(0);
    enableHttp2(server);
    server.route("/set", { "GET" }, [](auto req, auto resp) {
        resp->addCookie(Cookie{ .name = "session", .value = "abc123", .path = "/" });
        resp->setBody("ok");
    });
    server.route("/check", { "GET" }, [](auto req, auto resp) {
        resp->setBody(req->getCookie("session"));
    });
    startServer(server);
    co_await server.started();

    Http2Client client(h2cUrl(server.listeningPort()));
    co_await client.get("/set");
    auto resp = co_await client.get("/check");

    NITRO_CHECK_EQ(resp.body(), "");

    co_await server.stop();
}

// ── HTTPS fallback (HTTP/1.1) tests ──────────────────────────────────────────

// Builds a TLS-only HttpServer (no h2) with the given cert/key.
static void startHttp1TlsServer(http::HttpServer & server, const std::string & certPem, const std::string & keyPem)
{
    tls::TlsPolicy policy;
    policy.certPem = certPem;
    policy.keyPem = keyPem;
    policy.alpn = { "http/1.1" };
    policy.validate = false;
    auto ctx = tls::TlsContext::createServer(policy);
    server.setStreamUpgrader([ctx](net::TcpConnectionPtr conn) -> Task<io::StreamPtr> {
        auto tls = co_await tls::TlsStream::accept(conn, ctx);
        co_return std::make_shared<io::Stream>(std::move(tls));
    });
    Scheduler::current()->spawn([&server]() -> Task<> { co_await server.start(); });
}

static Http2Client makeFallbackClient(uint16_t port)
{
    Http2ClientConfig config;
    config.tls_policy.hostname = "localhost";
    config.tls_policy.validate = false;
    config.allow_http1_fallback = true;
    return Http2Client("https://localhost:" + std::to_string(port), config);
}

NITRO_TEST(https_fallback_get)
{
    auto [certPem, keyPem] = makeTestCert();
    http::HttpServer server(0);
    server.route("/hello", { "GET" }, [](auto req, auto resp) {
        resp->setBody("fallback");
    });
    startHttp1TlsServer(server, certPem, keyPem);
    co_await server.started();

    auto client = makeFallbackClient(server.listeningPort());
    auto resp = co_await client.get("/hello");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.body(), "fallback");

    co_await server.stop();
}

NITRO_TEST(https_fallback_post)
{
    auto [certPem, keyPem] = makeTestCert();
    http::HttpServer server(0);
    server.route("/echo", { "POST" }, [](auto req, auto resp) -> Task<> {
        auto complete = co_await req->toCompleteRequest();
        resp->setBody(complete.body());
    });
    startHttp1TlsServer(server, certPem, keyPem);
    co_await server.started();

    auto client = makeFallbackClient(server.listeningPort());
    auto resp = co_await client.post("/echo", "hello fallback");

    NITRO_CHECK_EQ(resp.statusCode(), 200);
    NITRO_CHECK_EQ(resp.body(), "hello fallback");

    co_await server.stop();
}

NITRO_TEST(https_fallback_multiple_requests)
{
    auto [certPem, keyPem] = makeTestCert();
    http::HttpServer server(0);
    server.route("/", { "GET" }, [](auto req, auto resp) {
        resp->setBody("ok");
    });
    startHttp1TlsServer(server, certPem, keyPem);
    co_await server.started();

    auto client = makeFallbackClient(server.listeningPort());
    auto r1 = co_await client.get("/");
    auto r2 = co_await client.get("/");

    NITRO_CHECK_EQ(r1.statusCode(), 200);
    NITRO_CHECK_EQ(r2.statusCode(), 200);

    co_await server.stop();
}

NITRO_TEST(https_fallback_disabled_throws)
{
    auto [certPem, keyPem] = makeTestCert();
    http::HttpServer server(0);
    server.route("/", { "GET" }, [](auto req, auto resp) { resp->setBody("ok"); });
    startHttp1TlsServer(server, certPem, keyPem);
    co_await server.started();

    Http2ClientConfig config;
    config.tls_policy.hostname = "localhost";
    config.tls_policy.validate = false;
    config.allow_http1_fallback = false;
    Http2Client client("https://localhost:" + std::to_string(server.listeningPort()), config);

    NITRO_CHECK_THROWS(co_await client.get("/"));

    co_await server.stop();
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
