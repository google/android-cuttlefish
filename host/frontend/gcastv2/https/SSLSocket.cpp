#include <https/SSLSocket.h>

#include <https/SafeCallbackable.h>
#include <https/Support.h>
#include <media/stagefright/foundation/ADebug.h>
#include <sstream>
#include <sys/socket.h>
#include <utils/KeyStore.h>

// static
void SSLSocket::Init() {
    SSL_library_init();
    SSL_load_error_strings();
}

// static
SSL_CTX *SSLSocket::CreateSSLContext() {
    SSL_CTX *ctx = SSL_CTX_new(SSLv23_method());

     /* Recommended to avoid SSLv2 & SSLv3 */
     SSL_CTX_set_options(
            ctx, SSL_OP_ALL | SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

    return ctx;
}

SSLSocket::SSLSocket(
        std::shared_ptr<RunLoop> rl, Mode mode, int sock, uint32_t flags)
    : BufferedSocket(rl, sock),
      mMode(mode),
      mFlags(flags),
      mCtx(CreateSSLContext(), SSL_CTX_free),
      mSSL(SSL_new(mCtx.get()), SSL_free),
      mBioR(BIO_new(BIO_s_mem())),
      mBioW(BIO_new(BIO_s_mem())),
      mEOS(false),
      mFinalErrno(0),
      mRecvPending(false),
      mRecvCallback(nullptr),
      mSendPending(false),
      mFlushFn(nullptr) {
    if (mMode == Mode::ACCEPT) {
        SSL_set_accept_state(mSSL.get());
    } else {
        SSL_set_connect_state(mSSL.get());
    }
    SSL_set_bio(mSSL.get(), mBioR, mBioW);
}

#ifdef TARGET_ANDROID_DEVICE

static bool isResourcePath(const std::string &path, std::string *name) {
    static const char *const kPrefix = "keystore:";
    static const size_t kPrefixLen = strlen(kPrefix);

    if (path.substr(0, kPrefixLen) != kPrefix) {
        return false;
    }

    *name = path.substr(kPrefixLen);

    return true;
}

#endif

bool SSLSocket::useCertificate(const std::string &path) {
#ifdef TARGET_ANDROID_DEVICE
    std::string name;
    if (isResourcePath(path, &name)) {
        std::vector<uint8_t> data;
        if (!getCertificateOrKey(name, &data)) {
            return false;
        }

        std::unique_ptr<BIO, std::function<void(BIO *)>> cbio(
                BIO_new_mem_buf(data.data(), data.size()), BIO_free);

        X509 *cert = PEM_read_bio_X509(cbio.get(), nullptr, 0, nullptr);

        return cert != nullptr && 1 == SSL_use_certificate(mSSL.get(), cert);
    }
#endif

    return 1 == SSL_use_certificate_file(
                mSSL.get(), path.c_str(), SSL_FILETYPE_PEM);
}

bool SSLSocket::usePrivateKey(const std::string &path) {
#ifdef TARGET_ANDROID_DEVICE
    std::string name;
    if (isResourcePath(path, &name)) {
        std::vector<uint8_t> data;
        if (!getCertificateOrKey(name, &data)) {
            return false;
        }

        std::unique_ptr<BIO, std::function<void(BIO *)>> cbio(
                BIO_new_mem_buf(data.data(), data.size()), BIO_free);

        RSA *key = PEM_read_bio_RSAPrivateKey(cbio.get(), nullptr, 0, nullptr);

        return key != nullptr
            && 1 == SSL_use_RSAPrivateKey(mSSL.get(), key)
            && 1 == SSL_check_private_key(mSSL.get());
    }
#endif

    return 1 == SSL_use_PrivateKey_file(
            mSSL.get(), path.c_str(), SSL_FILETYPE_PEM)
        && 1 == SSL_check_private_key(mSSL.get());
}

bool SSLSocket::useTrustedCertificates(const std::string &path) {
#ifdef TARGET_ANDROID_DEVICE
    std::string name;
    if (isResourcePath(path, &name)) {
        std::vector<uint8_t> data;
        if (!getCertificateOrKey(name, &data)) {
            return false;
        }

        std::unique_ptr<BIO, std::function<void(BIO *)>> cbio(
                BIO_new_mem_buf(data.data(), data.size()), BIO_free);

        // Shamelessly stolen from
        // https://stackoverflow.com/questions/3810058/
        //   read-certificate-files-from-memory-instead-of-a-file-using-openssl

        X509_STORE *cts = SSL_CTX_get_cert_store(mCtx.get());

        if (!cts) {
            return false;
        }

        STACK_OF(X509_INFO) *info =
            PEM_X509_INFO_read_bio(cbio.get(), nullptr, nullptr, nullptr);

        if (!info) {
            return false;
        }

        for (int i = 0; i < sk_X509_INFO_num(info); ++i) {
            X509_INFO *tmp = sk_X509_INFO_value(info, i);

            if (tmp->x509) {
                X509_STORE_add_cert(cts, tmp->x509);
            }

            if (tmp->crl) {
                X509_STORE_add_crl(cts, tmp->crl);
            }
        }

        sk_X509_INFO_pop_free(info, X509_INFO_free);

        return true;
    }
#endif

    return 1 == SSL_CTX_load_verify_locations(
            mCtx.get(),
            path.c_str(),
            nullptr /* CApath */);
}

SSLSocket::SSLSocket(
        std::shared_ptr<RunLoop> rl,
        int sock,
        const std::string &certificate_pem_path,
        const std::string &private_key_pem_path,
        uint32_t flags)
    : SSLSocket(rl, Mode::ACCEPT, sock, flags) {

    // This flag makes no sense for a server.
    CHECK(!(mFlags & FLAG_DONT_CHECK_PEER_CERTIFICATE));

    CHECK(useCertificate(certificate_pem_path)
            && usePrivateKey(private_key_pem_path));
}

SSLSocket::SSLSocket(
        std::shared_ptr<RunLoop> rl,
        int sock,
        uint32_t flags,
        const std::optional<std::string> &trusted_pem_path)
    : SSLSocket(rl, Mode::CONNECT, sock, flags) {

    if (!(mFlags & FLAG_DONT_CHECK_PEER_CERTIFICATE)) {
        CHECK(trusted_pem_path.has_value());
        CHECK(useTrustedCertificates(*trusted_pem_path));
    }
}

SSLSocket::~SSLSocket() {
    SSL_shutdown(mSSL.get());

    mBioW = mBioR = nullptr;
}

void SSLSocket::postRecv(RunLoop::AsyncFunction fn) {
    char tmp[128];
    int n = SSL_peek(mSSL.get(), tmp, sizeof(tmp));

    if (n > 0) {
        fn();
        return;
    }

    CHECK(mRecvCallback == nullptr);
    mRecvCallback = fn;

    if (!mRecvPending) {
        mRecvPending = true;
        runLoop()->postSocketRecv(
                fd(),
                makeSafeCallback(this, &SSLSocket::handleIncomingData));
    }
}

void SSLSocket::handleIncomingData() {
    mRecvPending = false;

    uint8_t buffer[1024];
    ssize_t len;
    do {
        len = ::recv(fd(), buffer, sizeof(buffer), 0);
    } while (len < 0 && errno == EINTR);

    if (len <= 0) {
        mEOS = true;
        mFinalErrno = (len < 0) ? errno : 0;

        sendRecvCallback();
        return;
    }

    size_t offset = 0;
    while (len > 0) {
        int n = BIO_write(mBioR, &buffer[offset], len);
        CHECK_GT(n, 0);

        offset += n;
        len -= n;

        if (!SSL_is_init_finished(mSSL.get())) {
            if (mMode == Mode::ACCEPT) {
                n = SSL_accept(mSSL.get());
            } else {
                n = SSL_connect(mSSL.get());
            }

            auto err = SSL_get_error(mSSL.get(), n);

            switch (err) {
                case SSL_ERROR_WANT_READ:
                {
                    CHECK_EQ(len, 0);
                    queueOutputDataFromSSL();

                    mRecvPending = true;

                    runLoop()->postSocketRecv(
                            fd(),
                            makeSafeCallback(
                                this, &SSLSocket::handleIncomingData));

                    return;
                }

                case SSL_ERROR_WANT_WRITE:
                {
                    CHECK_EQ(len, 0);

                    mRecvPending = true;

                    runLoop()->postSocketRecv(
                            fd(),
                            makeSafeCallback(
                                this, &SSLSocket::handleIncomingData));

                    return;
                }

                case SSL_ERROR_NONE:
                    break;

                case SSL_ERROR_SYSCALL:
                default:
                {
                    // This is where we end up if the client doesn't trust us.
                    mEOS = true;
                    mFinalErrno = ECONNREFUSED;

                    sendRecvCallback();
                    return;
                }
            }

            CHECK(SSL_is_init_finished(mSSL.get()));

            drainOutputBufferPlain();

            if (!(mFlags & FLAG_DONT_CHECK_PEER_CERTIFICATE)
                    && !isPeerCertificateValid()) {
                mEOS = true;
                mFinalErrno = ECONNREFUSED;
                sendRecvCallback();
            }
        }
    }

    int n = SSL_peek(mSSL.get(), buffer, sizeof(buffer));

    if (n > 0) {
        sendRecvCallback();
        return;
    }

    auto err = SSL_get_error(mSSL.get(), n);

    switch (err) {
        case SSL_ERROR_WANT_READ:
        {
            queueOutputDataFromSSL();

            mRecvPending = true;

            runLoop()->postSocketRecv(
                    fd(),
                    makeSafeCallback(this, &SSLSocket::handleIncomingData));

            break;
        }

        case SSL_ERROR_WANT_WRITE:
        {
            mRecvPending = true;

            runLoop()->postSocketRecv(
                    fd(),
                    makeSafeCallback(this, &SSLSocket::handleIncomingData));

            break;
        }

        case SSL_ERROR_ZERO_RETURN:
        {
            mEOS = true;
            mFinalErrno = 0;

            sendRecvCallback();
            break;
        }

        case SSL_ERROR_NONE:
            break;

        case SSL_ERROR_SYSCALL:
        default:
        {
            // This is where we end up if the client doesn't trust us.
            mEOS = true;
            mFinalErrno = ECONNREFUSED;

            sendRecvCallback();
            break;
        }
    }
}

void SSLSocket::sendRecvCallback() {
    const auto cb = mRecvCallback;
    mRecvCallback = nullptr;
    if (cb != nullptr) {
        cb();
    }
}

void SSLSocket::postSend(RunLoop::AsyncFunction fn) {
    runLoop()->post(fn);
}

ssize_t SSLSocket::recvfrom(
        void *data,
        size_t size,
        sockaddr *address,
        socklen_t *addressLen) {
    if (address || addressLen) {
        errno = EINVAL;
        return -1;
    }

    if (mEOS) {
        errno = mFinalErrno;
        return (mFinalErrno == 0) ? 0 : -1;
    }

    int n = SSL_read(mSSL.get(), data, size);

    // We should only get here after SSL_peek signaled that there's data to
    // be read.
    CHECK_GT(n, 0);

    return n;
}

void SSLSocket::queueOutputDataFromSSL() {
    int n;
    do {
        char buf[1024];
        n = BIO_read(mBioW, buf, sizeof(buf));

        if (n > 0) {
            queueOutputData(buf, n);
        } else if (BIO_should_retry(mBioW)) {
            continue;
        } else {
            TRESPASS();
        }
    } while (n > 0);
}

void SSLSocket::queueOutputData(const void *data, size_t size) {
    if (!size) {
        return;
    }

    const size_t pos = mOutBuffer.size();
    mOutBuffer.resize(pos + size);
    memcpy(mOutBuffer.data() + pos, data, size);

    if (!mSendPending) {
        mSendPending = true;
        runLoop()->postSocketSend(
                fd(),
                makeSafeCallback(this, &SSLSocket::sendOutputData));
    }
}

void SSLSocket::sendOutputData() {
    mSendPending = false;

    const size_t size = mOutBuffer.size();
    size_t offset = 0;

    while (offset < size) {
        ssize_t n = ::send(
                fd(), mOutBuffer.data() + offset, size - offset, 0);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            TRESPASS();
        }

        offset += static_cast<size_t>(n);
    }

    mOutBuffer.erase(mOutBuffer.begin(), mOutBuffer.begin() + offset);

    if (!mOutBufferPlain.empty()) {
        drainOutputBufferPlain();
    }

    if (!mOutBuffer.empty()) {
        mSendPending = true;
        runLoop()->postSocketSend(
                fd(),
                makeSafeCallback(this, &SSLSocket::sendOutputData));

        return;
    }

    auto fn = mFlushFn;
    mFlushFn = nullptr;
    if (fn != nullptr) {
        fn();
    }
}

ssize_t SSLSocket::sendto(
        const void *data,
        size_t size,
        const sockaddr *addr,
        socklen_t addrLen) {
    if (addr || addrLen) {
        errno = -EINVAL;
        return -1;
    }

    if (mEOS) {
        errno = mFinalErrno;
        return (mFinalErrno == 0) ? 0 : -1;
    }

    const size_t pos = mOutBufferPlain.size();
    mOutBufferPlain.resize(pos + size);
    memcpy(&mOutBufferPlain[pos], data, size);

    drainOutputBufferPlain();

    return size;
}

void SSLSocket::drainOutputBufferPlain() {
    size_t offset = 0;
    const size_t size = mOutBufferPlain.size();

    while (offset < size) {
        int n = SSL_write(mSSL.get(), &mOutBufferPlain[offset], size - offset);

        if (!SSL_is_init_finished(mSSL.get())) {
            if (mMode == Mode::ACCEPT) {
                n = SSL_accept(mSSL.get());
            } else {
                n = SSL_connect(mSSL.get());
            }

            auto err = SSL_get_error(mSSL.get(), n);

            switch (err) {
                case SSL_ERROR_WANT_WRITE:
                {
                    mOutBufferPlain.erase(
                            mOutBufferPlain.begin(),
                            mOutBufferPlain.begin() + offset);

                    queueOutputDataFromSSL();
                    return;
                }

                case SSL_ERROR_WANT_READ:
                {
                    mOutBufferPlain.erase(
                            mOutBufferPlain.begin(),
                            mOutBufferPlain.begin() + offset);

                    queueOutputDataFromSSL();

                    if (!mRecvPending) {
                        mRecvPending = true;

                        runLoop()->postSocketRecv(
                                fd(),
                                makeSafeCallback(
                                    this, &SSLSocket::handleIncomingData));
                    }
                    return;
                }

                case SSL_ERROR_SYSCALL:
                {
                    // This is where we end up if the client doesn't trust us.
                    mEOS = true;
                    mFinalErrno = ECONNREFUSED;

                    TRESPASS();
                    return;
                }

                case SSL_ERROR_NONE:
                    break;

                default:
                    TRESPASS();
            }

            CHECK(SSL_is_init_finished(mSSL.get()));

            if (!isPeerCertificateValid()) {
                mEOS = true;
                mFinalErrno = ECONNREFUSED;
                sendRecvCallback();
            }
        }

        offset += n;
    }

    mOutBufferPlain.erase(
            mOutBufferPlain.begin(), mOutBufferPlain.begin() + offset);

    queueOutputDataFromSSL();
}

bool SSLSocket::isPeerCertificateValid() {
    if (mMode == Mode::ACCEPT || (mFlags & FLAG_DONT_CHECK_PEER_CERTIFICATE)) {
        // For now we won't validate the client if we are the server.
        return true;
    }

    std::unique_ptr<X509, std::function<void(X509 *)>> cert(
            SSL_get_peer_certificate(mSSL.get()), X509_free);

    if (!cert) {
        LOG(ERROR) << "SSLSocket::isPeerCertificateValid no certificate.";

        return false;
    }

    int res = SSL_get_verify_result(mSSL.get());

    bool valid = (res == X509_V_OK);

    if (!valid) {
        LOG(ERROR) << "SSLSocket::isPeerCertificateValid invalid certificate.";

        const EVP_MD *digest = EVP_get_digestbyname("sha256");

        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int n;
        int res = X509_digest(cert.get(), digest, md, &n);
        CHECK_EQ(res, 1);

        std::stringstream ss;
        for (unsigned int i = 0; i < n; ++i) {
            if (i > 0) {
                ss << ":";
            }

            auto byte = md[i];

            auto nibble = byte >> 4;
            ss << (char)((nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10));

            nibble = byte & 0x0f;
            ss << (char)((nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10));
        }

        LOG(ERROR)
            << "Server offered certificate w/ fingerprint "
            << ss.str();
    }

    return valid;
}

void SSLSocket::postFlush(RunLoop::AsyncFunction fn) {
    CHECK(mFlushFn == nullptr);

    if (!mSendPending) {
        fn();
        return;
    }

    mFlushFn = fn;
}

