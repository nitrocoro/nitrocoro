/**
 * @file tls_test.cc
 * @brief Unit tests for TlsStream
 */
#include <nitrocoro/core/Scheduler.h>
#include <nitrocoro/net/InetAddress.h>
#include <nitrocoro/net/TcpConnection.h>
#include <nitrocoro/net/TcpServer.h>
#include <nitrocoro/testing/Test.h>
#include <nitrocoro/tls/TlsContext.h>
#include <nitrocoro/tls/TlsPolicy.h>
#include <nitrocoro/tls/TlsStream.h>

#include <cstring>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

using namespace nitrocoro;
using namespace nitrocoro::tls;
using namespace nitrocoro::net;

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

/** Start a one-shot TLS server on a random port; returns the port chosen. */
struct TestServer
{
    TcpServer server;

    explicit TestServer(uint16_t p)
        : server(p) {}
};

/** Build server/client TlsContext from an in-memory self-signed cert. */
static std::pair<TlsContextPtr, TlsContextPtr> makeContexts(const std::string & cn = "localhost",
                                                            std::vector<std::string> alpn = {})
{
    auto [certPem, keyPem] = makeTestCert(cn);

    TlsPolicy sp;
    sp.certPem = certPem;
    sp.keyPem = keyPem;
    sp.validate = false;
    sp.alpn = alpn;
    auto serverCtx = TlsContext::create(sp, true);

    TlsPolicy cp;
    cp.hostname = cn;
    cp.validate = false;
    cp.alpn = alpn;
    auto clientCtx = TlsContext::create(cp, false);

    return { serverCtx, clientCtx };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/** Handshake completes without error on both sides. */
NITRO_TEST(tls_handshake)
{
    auto [serverCtx, clientCtx] = makeContexts();
    TcpServer server(0);

    Scheduler::current()->spawn([&]() mutable -> Task<> {
        co_await server.start([&](TcpConnectionPtr conn) mutable -> Task<> {
            auto tls = co_await TlsStream::accept(conn, serverCtx);
            NITRO_CHECK(tls != nullptr);
            co_await tls->shutdown();
            co_await server.stop();
        });
    });

    co_await Scheduler::current()->sleep_for(0.01);

    auto conn = co_await TcpConnection::connect({"127.0.0.1", server.port()});
    auto tls = co_await TlsStream::connect(conn, clientCtx);
    NITRO_CHECK(tls != nullptr);
    co_await tls->shutdown();
    co_await server.wait();
}

/** Client sends data, server echoes it back, content matches. */
NITRO_TEST(tls_echo)
{
    auto [serverCtx, clientCtx] = makeContexts();
    TcpServer server(0);

    Scheduler::current()->spawn([&]() mutable -> Task<> {
        co_await server.start([&](TcpConnectionPtr conn) mutable -> Task<> {
            auto tls = co_await TlsStream::accept(conn, serverCtx);
            char buf[256];
            size_t n = co_await tls->read(buf, sizeof(buf));
            co_await tls->write(buf, n);
            co_await tls->shutdown();
            co_await server.stop();
        });
    });

    co_await Scheduler::current()->sleep_for(0.01);

    auto conn = co_await TcpConnection::connect({"127.0.0.1", server.port()});
    auto tls = co_await TlsStream::connect(conn, clientCtx);

    const std::string msg = "hello tls";
    co_await tls->write(msg.data(), msg.size());

    char buf[256];
    size_t n = co_await tls->read(buf, sizeof(buf));
    NITRO_CHECK_EQ(n, msg.size());
    NITRO_CHECK(std::string(buf, n) == msg);

    co_await tls->shutdown();
    co_await server.wait();
}

/** ALPN negotiation selects the first mutually supported protocol. */
NITRO_TEST(tls_alpn)
{
    auto [serverCtx, clientCtx] = makeContexts("localhost", { "h2", "http/1.1" });
    auto serverAlpn = std::make_shared<std::string>();
    TcpServer server(0);

    Scheduler::current()->spawn([&]() mutable -> Task<> {
        co_await server.start([&](TcpConnectionPtr conn) mutable -> Task<> {
            auto tls = co_await TlsStream::accept(conn, serverCtx);
            *serverAlpn = tls->negotiatedAlpn();
            co_await tls->shutdown();
            co_await server.stop();
        });
    });

    co_await Scheduler::current()->sleep_for(0.01);

    auto conn = co_await TcpConnection::connect({"127.0.0.1", server.port()});
    auto tls = co_await TlsStream::connect(conn, clientCtx);
    NITRO_CHECK_EQ(tls->negotiatedAlpn(), std::string("h2"));
    co_await tls->shutdown();
    co_await server.wait();
    NITRO_CHECK_EQ(*serverAlpn, std::string("h2"));
}

/** Server receives the SNI hostname sent by the client. */
NITRO_TEST(tls_sni)
{
    auto [serverCtx, clientCtx] = makeContexts("myhost.example");
    auto serverSni = std::make_shared<std::string>();
    TcpServer server(0);

    Scheduler::current()->spawn([&]() mutable -> Task<> {
        co_await server.start([&](TcpConnectionPtr conn) mutable -> Task<> {
            auto tls = co_await TlsStream::accept(conn, serverCtx);
            *serverSni = tls->sniName();
            co_await tls->shutdown();
            co_await server.stop();
        });
    });

    co_await Scheduler::current()->sleep_for(0.01);

    auto conn = co_await TcpConnection::connect({"127.0.0.1", server.port()});
    auto tls = co_await TlsStream::connect(conn, clientCtx);
    co_await tls->shutdown();
    co_await server.wait();
    NITRO_CHECK_EQ(*serverSni, std::string("myhost.example"));
}

/** Transfer data larger than a single TLS record (>16 KB). */
NITRO_TEST(tls_large_transfer)
{
    auto [serverCtx, clientCtx] = makeContexts();
    const size_t dataSize = 128 * 1024;
    auto received = std::make_shared<std::vector<char>>();
    TcpServer server(0);

    Scheduler::current()->spawn([&]() mutable -> Task<> {
        co_await server.start([&](TcpConnectionPtr conn) mutable -> Task<> {
            auto tls = co_await TlsStream::accept(conn, serverCtx);
            char tmp[16384];
            while (received->size() < dataSize)
            {
                size_t n = co_await tls->read(tmp, sizeof(tmp));
                if (n == 0)
                    break;
                received->insert(received->end(), tmp, tmp + n);
            }
            co_await tls->shutdown();
            co_await server.stop();
        });
    });

    co_await Scheduler::current()->sleep_for(0.01);

    auto conn = co_await TcpConnection::connect({"127.0.0.1", server.port()});
    auto tls = co_await TlsStream::connect(conn, clientCtx);

    std::vector<char> payload(dataSize, 'x');
    co_await tls->write(payload.data(), payload.size());
    co_await tls->shutdown();
    co_await server.wait();

    NITRO_CHECK_EQ(received->size(), dataSize);
    NITRO_CHECK(*received == payload);
}

/** After clean shutdown, server read returns 0 (EOF). */
NITRO_TEST(tls_shutdown_eof)
{
    auto [serverCtx, clientCtx] = makeContexts();
    auto serverReadN = std::make_shared<size_t>(0);
    TcpServer server(0);

    Scheduler::current()->spawn([&]() mutable -> Task<> {
        co_await server.start([&](TcpConnectionPtr conn) mutable -> Task<> {
            auto tls = co_await TlsStream::accept(conn, serverCtx);
            char buf[64];
            *serverReadN = co_await tls->read(buf, sizeof(buf));
            co_await server.stop();
        });
    });

    co_await Scheduler::current()->sleep_for(0.01);

    auto conn = co_await TcpConnection::connect({"127.0.0.1", server.port()});
    auto tls = co_await TlsStream::connect(conn, clientCtx);
    co_await tls->shutdown();
    co_await server.wait();
    NITRO_CHECK_EQ(*serverReadN, size_t(0));
}

/** Client with validate=true rejects a self-signed server cert. */
NITRO_TEST(tls_bad_cert_rejected)
{
    auto [certPem, keyPem] = makeTestCert("localhost");

    TlsPolicy sp;
    sp.certPem = certPem;
    sp.keyPem = keyPem;
    sp.validate = false;
    auto serverCtx = TlsContext::create(sp, true);

    TlsPolicy cp;
    cp.hostname = "localhost";
    cp.validate = true;
    cp.useSystemCertStore = false;
    auto clientCtx = TlsContext::create(cp, false);

    TcpServer server(0);

    Scheduler::current()->spawn([&]() mutable -> Task<> {
        co_await server.start([&](TcpConnectionPtr conn) mutable -> Task<> {
            try
            {
                co_await TlsStream::accept(conn, serverCtx);
            }
            catch (...)
            {
            }
            co_await server.stop();
        });
    });

    co_await Scheduler::current()->sleep_for(0.01);

    auto conn = co_await TcpConnection::connect({"127.0.0.1", server.port()});
    bool threw = false;
    try
    {
        co_await TlsStream::connect(conn, clientCtx);
    }
    catch (const std::exception &)
    {
        threw = true;
    }
    NITRO_CHECK(threw);
    co_await server.wait();
}

/** sniResolver routes two different hostnames to two different TlsContexts. */
NITRO_TEST(tls_multi_sni)
{
    auto [certPemA, keyPemA] = makeTestCert("a.example");
    auto [certPemB, keyPemB] = makeTestCert("b.example");

    auto makeServerCtx = [](const std::string & certPem, const std::string & keyPem) {
        TlsPolicy sp;
        sp.certPem = certPem;
        sp.keyPem = keyPem;
        sp.validate = false;
        return TlsContext::create(sp, true);
    };

    auto ctxA = makeServerCtx(certPemA, keyPemA);
    auto ctxB = makeServerCtx(certPemB, keyPemB);
    ctxA->sniResolver = [ctxB](std::string_view sni) -> TlsContextPtr {
        return sni == "b.example" ? ctxB : nullptr;
    };

    std::vector<std::string> names;
    TcpServer server(0);

    Scheduler::current()->spawn([&]() -> Task<> {
        co_await server.start([&](TcpConnectionPtr conn) -> Task<> {
            auto tls = co_await TlsStream::accept(conn, ctxA);
            auto sniName = tls->sniName();
            names.push_back(sniName);
            co_await tls->shutdown();
            if (names.size() == 2)
                co_await server.stop();
        });
    });

    co_await Scheduler::current()->sleep_for(0.5);

    auto connectAs = [](uint16_t port, const std::string & host) -> Task<TlsStreamPtr> {
        TlsPolicy cp;
        cp.hostname = host;
        cp.validate = false;
        auto clientCtx = TlsContext::create(cp, false);
        auto conn = co_await TcpConnection::connect({"127.0.0.1", port});
        co_return co_await TlsStream::connect(conn, clientCtx);
    };

    auto tlsA = co_await connectAs(server.port(), "a.example");
    auto tlsB = co_await connectAs(server.port(), "b.example");
    co_await tlsA->shutdown();
    co_await tlsB->shutdown();
    co_await server.wait();

    NITRO_REQUIRE_EQ(names.size(), 2u);
    bool hasA = names[0] == "a.example" || names[1] == "a.example";
    bool hasB = names[0] == "b.example" || names[1] == "b.example";
    NITRO_CHECK(hasA);
    NITRO_CHECK(hasB);
}

int main(int argc, char ** argv)
{
    return nitrocoro::test::run_all(argc, argv);
}
