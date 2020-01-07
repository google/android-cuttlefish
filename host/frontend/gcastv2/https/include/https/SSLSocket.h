/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <https/BufferedSocket.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <memory>

struct SSLSocket
    : public BufferedSocket,
      public std::enable_shared_from_this<SSLSocket> {
    static void Init();

    enum {
        // Client only.
        FLAG_DONT_CHECK_PEER_CERTIFICATE = 1,
    };

    explicit SSLSocket(
            std::shared_ptr<RunLoop> rl,
            int sock,
            const std::string &certificate_pem_path,
            const std::string &private_key_pem_path,
            uint32_t flags = 0);

    explicit SSLSocket(
            std::shared_ptr<RunLoop> rl,
            int sock,
            uint32_t flags = 0,
            const std::optional<std::string> &trusted_pem_path = std::nullopt);

    ~SSLSocket() override;

    SSLSocket(const SSLSocket &) = delete;
    SSLSocket &operator=(const SSLSocket &) = delete;

    void postRecv(RunLoop::AsyncFunction fn) override;
    void postSend(RunLoop::AsyncFunction fn) override;

    ssize_t recvfrom(
            void *data,
            size_t size,
            sockaddr *address,
            socklen_t *addressLen) override;

    ssize_t sendto(
            const void *data,
            size_t size,
            const sockaddr *addr,
            socklen_t addrLen) override;

    void postFlush(RunLoop::AsyncFunction fn) override;

private:
    enum class Mode {
        CONNECT,
        ACCEPT,
    };

    Mode mMode;
    uint32_t mFlags;

    std::unique_ptr<SSL_CTX, std::function<void(SSL_CTX *)>> mCtx;
    std::unique_ptr<SSL, std::function<void(SSL *)>> mSSL;

    // These are owned by the SSL object.
    BIO *mBioR;
    BIO *mBioW;

    bool mEOS;
    int mFinalErrno;

    bool mRecvPending;
    RunLoop::AsyncFunction mRecvCallback;

    bool mSendPending;
    std::vector<uint8_t> mOutBuffer;

    // This is as yet unencrypted data that has yet to be submitted to SSL_write.
    std::vector<uint8_t> mOutBufferPlain;

    RunLoop::AsyncFunction mFlushFn;

    explicit SSLSocket(
            std::shared_ptr<RunLoop> rl, Mode mode, int sock, uint32_t flags);

    void handleIncomingData();
    void sendRecvCallback();

    void queueOutputDataFromSSL();

    void queueOutputData(const void *data, size_t size);
    void sendOutputData();

    void drainOutputBufferPlain();
    bool isPeerCertificateValid();

    static SSL_CTX *CreateSSLContext();

    bool useCertificate(const std::string &path);
    bool usePrivateKey(const std::string &path);
    bool useTrustedCertificates(const std::string &path);
};
