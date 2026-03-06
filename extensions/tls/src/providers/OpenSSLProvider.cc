/**
 * @file OpenSSLProvider.cc
 * @brief OpenSSL TLS provider implementation
 */
#include <nitrocoro/tls/TlsContext.h>
#include <nitrocoro/tls/TlsProvider.h>
#include <nitrocoro/utils/Debug.h>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <cstring>
#include <stdexcept>

namespace nitrocoro::tls
{

// ---------------------------------------------------------------------------
// OpenSSLContext
// ---------------------------------------------------------------------------

class OpenSSLContext : public TlsContext
{
public:
    OpenSSLContext(const TlsPolicy & policy, bool isServer)
        : isServer_(isServer)
        , policy_(policy)
    {
        ctx_ = SSL_CTX_new(TLS_method());
        if (!ctx_)
            throw std::runtime_error("SSL_CTX_new failed");

        SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);

        if (!policy.certPem.empty())
        {
            BIO * bio = BIO_new_mem_buf(policy.certPem.data(), static_cast<int>(policy.certPem.size()));
            X509 * cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
            if (!cert || SSL_CTX_use_certificate(ctx_, cert) <= 0)
            {
                X509_free(cert);
                throw std::runtime_error("Failed to load certificate from PEM");
            }
            X509_free(cert);
        }
        else if (!policy.certPath.empty())
        {
            if (SSL_CTX_use_certificate_chain_file(ctx_, policy.certPath.c_str()) <= 0)
                throw std::runtime_error("Failed to load certificate: " + policy.certPath);
        }
        if (!policy.keyPem.empty())
        {
            BIO * bio = BIO_new_mem_buf(policy.keyPem.data(), static_cast<int>(policy.keyPem.size()));
            EVP_PKEY * key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
            if (!key || SSL_CTX_use_PrivateKey(ctx_, key) <= 0)
            {
                EVP_PKEY_free(key);
                throw std::runtime_error("Failed to load private key from PEM");
            }
            EVP_PKEY_free(key);
            if (SSL_CTX_check_private_key(ctx_) == 0)
                throw std::runtime_error("Private key does not match certificate");
        }
        else if (!policy.keyPath.empty())
        {
            if (SSL_CTX_use_PrivateKey_file(ctx_, policy.keyPath.c_str(), SSL_FILETYPE_PEM) <= 0)
                throw std::runtime_error("Failed to load private key: " + policy.keyPath);
            if (SSL_CTX_check_private_key(ctx_) == 0)
                throw std::runtime_error("Private key does not match certificate");
        }

        if (policy.validate)
        {
            if (policy.useSystemCertStore)
                SSL_CTX_set_default_verify_paths(ctx_);
            if (!policy.caPath.empty())
            {
                if (SSL_CTX_load_verify_locations(ctx_, policy.caPath.c_str(), nullptr) <= 0)
                    throw std::runtime_error("Failed to load CA: " + policy.caPath);
            }
        }
        else
        {
            SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
        }

        if (!policy.alpn.empty() && isServer)
            SSL_CTX_set_alpn_select_cb(ctx_, alpnSelectCb, this);

        if (isServer)
        {
            SSL_CTX_set_tlsext_servername_callback(ctx_, sniCb);
            SSL_CTX_set_tlsext_servername_arg(ctx_, this);
        }
    }

    ~OpenSSLContext() override
    {
        if (ctx_)
            SSL_CTX_free(ctx_);
    }

    std::unique_ptr<TlsProvider> newProvider() override;

    SSL_CTX * ctx() const { return ctx_; }
    bool isServer() const { return isServer_; }
    const TlsPolicy & policy() const { return policy_; }

private:
    static int alpnSelectCb(SSL *, const unsigned char ** out, unsigned char * outlen,
                            const unsigned char * in, unsigned int inlen, void * arg)
    {
        auto * self = static_cast<OpenSSLContext *>(arg);
        for (const auto & proto : self->policy_.alpn)
        {
            const unsigned char * cur = in;
            const unsigned char * end = in + inlen;
            while (cur < end)
            {
                unsigned int plen = *cur++;
                if (cur + plen > end)
                    break;
                if (proto.size() == plen && memcmp(cur, proto.data(), plen) == 0)
                {
                    *out = cur;
                    *outlen = static_cast<unsigned char>(plen);
                    return SSL_TLSEXT_ERR_OK;
                }
                cur += plen;
            }
        }
        return SSL_TLSEXT_ERR_NOACK;
    }

    static int sniCb(SSL * ssl, int *, void * arg)
    {
        auto * self = static_cast<OpenSSLContext *>(arg);
        if (!self->sniResolver)
            return SSL_TLSEXT_ERR_OK;
        const char * name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
        if (!name)
            return SSL_TLSEXT_ERR_OK;
        auto newCtx = self->sniResolver(name);
        if (!newCtx)
            return SSL_TLSEXT_ERR_OK;
        if (auto * oc = dynamic_cast<OpenSSLContext *>(newCtx.get()))
            SSL_set_SSL_CTX(ssl, oc->ctx());
        return SSL_TLSEXT_ERR_OK;
    }

    SSL_CTX * ctx_{ nullptr };
    bool isServer_;
    TlsPolicy policy_;
};

// ---------------------------------------------------------------------------
// OpenSSLProvider
// ---------------------------------------------------------------------------

class OpenSSLProvider : public TlsProvider
{
public:
    explicit OpenSSLProvider(OpenSSLContext * ctx)
        : ctx_(ctx)
    {
        rbio_ = BIO_new(BIO_s_mem());
        wbio_ = BIO_new(BIO_s_mem());
        ssl_ = SSL_new(ctx->ctx());
        if (!ssl_ || !rbio_ || !wbio_)
            throw std::runtime_error("OpenSSL object creation failed");
        SSL_set_bio(ssl_, rbio_, wbio_);

        if (!ctx->isServer() && !ctx->policy().hostname.empty())
            SSL_set_tlsext_host_name(ssl_, ctx->policy().hostname.c_str());
    }

    ~OpenSSLProvider() override
    {
        if (ssl_)
            SSL_free(ssl_);
    }

    void startHandshake(std::vector<char> & encOut) override
    {
        if (ctx_->isServer())
        {
            SSL_set_accept_state(ssl_);
        }
        else
        {
            if (!ctx_->policy().alpn.empty())
            {
                std::string wire;
                for (const auto & p : ctx_->policy().alpn)
                {
                    wire.push_back(static_cast<char>(p.size()));
                    wire.append(p);
                }
                SSL_set_alpn_protos(ssl_,
                                    reinterpret_cast<const unsigned char *>(wire.data()),
                                    static_cast<unsigned int>(wire.size()));
            }
            SSL_set_connect_state(ssl_);
        }
        // Client sends ClientHello on first handshake step
        if (!ctx_->isServer())
        {
            SSL_do_handshake(ssl_);
            flushWbio(encOut);
        }
    }

    FeedResult feedEncrypted(const void * data, size_t len,
                             std::vector<char> & plainOut,
                             std::vector<char> & encOut) override
    {
        BIO_write(rbio_, data, static_cast<int>(len));

        if (!SSL_is_init_finished(ssl_))
        {
            int ret = SSL_do_handshake(ssl_);
            flushWbio(encOut);

            if (ret == 1)
            {
                if (!finishHandshake())
                    return FeedResult::Error;
                // Drain any application data that arrived with the final handshake flight
                drainPlaintext(plainOut);
                return FeedResult::HandshakeDone;
            }

            int err = SSL_get_error(ssl_, ret);
            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
            {
                lastError_ = sslError("TLS handshake failed");
                return FeedResult::Error;
            }
            return FeedResult::Ok;
        }

        drainPlaintext(plainOut);
        flushWbio(encOut); // session tickets etc.

        if (!lastError_.empty())
            return FeedResult::Error;
        if (eof_)
            return FeedResult::Eof;
        return FeedResult::Ok;
    }

    ssize_t sendPlain(const void * data, size_t len, std::vector<char> & encOut) override
    {
        int n = SSL_write(ssl_, data, static_cast<int>(len));
        if (n <= 0)
        {
            int err = SSL_get_error(ssl_, n);
            // TODO: need to handle renegotiation here
            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ)
                return 0;
            lastError_ = sslError("SSL_write failed");
            return -1;
        }
        flushWbio(encOut);
        return n;
    }

    void close(std::vector<char> & encOut) override
    {
        if (SSL_is_init_finished(ssl_))
        {
            SSL_shutdown(ssl_);
            flushWbio(encOut);
        }
    }

    std::string sniName() const override { return sniName_; }
    std::string negotiatedAlpn() const override { return alpn_; }
    std::string lastError() const override { return lastError_; }

private:
    bool finishHandshake()
    {
        const char * sni = SSL_get_servername(ssl_, TLSEXT_NAMETYPE_host_name);
        if (sni)
            sniName_ = sni;
        else if (!ctx_->isServer())
            sniName_ = ctx_->policy().hostname;

        const unsigned char * alpn = nullptr;
        unsigned int alpnLen = 0;
        SSL_get0_alpn_selected(ssl_, &alpn, &alpnLen);
        if (alpn)
            alpn_.assign(reinterpret_cast<const char *>(alpn), alpnLen);

        if (ctx_->policy().validate)
        {
            X509 * cert = SSL_get_peer_certificate(ssl_);
            if (!cert)
            {
                lastError_ = "No peer certificate";
                return false;
            }
            X509_free(cert);
            long result = SSL_get_verify_result(ssl_);
            if (result != X509_V_OK)
            {
                lastError_ = std::string("Certificate verification failed: ")
                             + X509_verify_cert_error_string(result);
                return false;
            }
        }
        return true;
    }

    void drainPlaintext(std::vector<char> & out)
    {
        char tmp[16384];
        int n;
        while ((n = SSL_read(ssl_, tmp, sizeof(tmp))) > 0)
            out.insert(out.end(), tmp, tmp + n);

        int err = SSL_get_error(ssl_, n);
        if (err == SSL_ERROR_ZERO_RETURN)
            eof_ = true;
        else if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE && err != SSL_ERROR_NONE)
            lastError_ = sslError("SSL_read failed");
    }

    void flushWbio(std::vector<char> & out)
    {
        char tmp[16384];
        int n;
        while ((n = BIO_read(wbio_, tmp, sizeof(tmp))) > 0)
            out.insert(out.end(), tmp, tmp + n);
    }

    static std::string sslError(std::string prefix)
    {
        unsigned long e = ERR_get_error();
        if (e)
        {
            char buf[256];
            ERR_error_string_n(e, buf, sizeof(buf));
            prefix += ": ";
            prefix += buf;
        }
        return prefix;
    }

    OpenSSLContext * ctx_;
    SSL * ssl_{ nullptr };
    BIO * rbio_{ nullptr };
    BIO * wbio_{ nullptr };
    bool eof_{ false };
    std::string sniName_;
    std::string alpn_;
    std::string lastError_;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<TlsProvider> OpenSSLContext::newProvider()
{
    return std::make_unique<OpenSSLProvider>(this);
}

TlsContextPtr TlsContext::create(const TlsPolicy & policy, bool isServer)
{
    return std::make_shared<OpenSSLContext>(policy, isServer);
}

} // namespace nitrocoro::tls
