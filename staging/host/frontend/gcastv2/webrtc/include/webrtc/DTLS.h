#pragma once

#include <https/RunLoop.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>

#include <functional>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <vector>

#include <srtp2/srtp.h>

struct RTPSocketHandler;

struct DTLS : public std::enable_shared_from_this<DTLS> {
    static void Init();

    enum class Mode {
        ACCEPT,
        CONNECT
    };

    explicit DTLS(
            std::shared_ptr<RTPSocketHandler> handler,
            Mode mode,
            std::shared_ptr<X509> certificate,
            std::shared_ptr<EVP_PKEY> key,
            const std::string &remoteFingerprint,
            bool useSRTP);

    ~DTLS();

    void connect(const sockaddr_storage &remoteAddr);
    void inject(const uint8_t *data, size_t size);

    size_t protect(void *data, size_t size, bool isRTP);
    size_t unprotect(void *data, size_t size, bool isRTP);

    // Returns -EAGAIN if no data is currently available.
    ssize_t readApplicationData(void *data, size_t size);

    ssize_t writeApplicationData(const void *data, size_t size);

private:
    enum class State {
        UNINITIALIZED,
        CONNECTING,
        CONNECTED,

    } mState;

    std::weak_ptr<RTPSocketHandler> mHandler;
    Mode mMode;
    std::string mRemoteFingerprint;
    bool mUseSRTP;

    SSL_CTX *mCtx;
    SSL *mSSL;

    // These are owned by the SSL object.
    BIO *mBioR, *mBioW;

    sockaddr_storage mRemoteAddr;

    srtp_t mSRTPInbound, mSRTPOutbound;

    static int OnVerifyPeerCertificate(int ok, X509_STORE_CTX *ctx);

    void doTheThing(int res);
    void queueOutputDataFromDTLS();
    void tryConnecting();

    void getKeyingMaterial();

    static void CreateSRTPSession(
            srtp_t *session,
            const std::string &keyAndSalt,
            srtp_ssrc_type_t direction);

    bool useCertificate(std::shared_ptr<X509> certificate);
    bool usePrivateKey(std::shared_ptr<EVP_PKEY> key);
};
