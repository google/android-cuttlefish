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

#include <webrtc/DTLS.h>

#include <webrtc/RTPSocketHandler.h>

#include <https/SafeCallbackable.h>
#include <https/SSLSocket.h>
#include <https/Support.h>

#include <android-base/logging.h>

#include <sys/socket.h>
#include <unistd.h>

#include <sstream>

static int gDTLSInstanceIndex;

// static
void DTLS::Init() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    auto err = srtp_init();
    CHECK_EQ(err, srtp_err_status_ok);

    gDTLSInstanceIndex = SSL_get_ex_new_index(
            0, const_cast<char *>("DTLSInstance index"), NULL, NULL, NULL);

}

bool DTLS::useCertificate(std::shared_ptr<X509> cert) {
    // I'm assuming that ownership of the certificate is transferred, so I'm
    // adding an extra reference...
    CHECK_EQ(1, X509_up_ref(cert.get()));

    return cert != nullptr && 1 == SSL_CTX_use_certificate(mCtx, cert.get());
}

bool DTLS::usePrivateKey(std::shared_ptr<EVP_PKEY> key) {
    // I'm assuming that ownership of the key in SSL_CTX_use_PrivateKey is
    // transferred, so I'm adding an extra reference...
    CHECK_EQ(1, EVP_PKEY_up_ref(key.get()));

    return key != nullptr
        && 1 == SSL_CTX_use_PrivateKey(mCtx, key.get())
        && 1 == SSL_CTX_check_private_key(mCtx);
}

DTLS::DTLS(
        std::shared_ptr<RTPSocketHandler> handler,
        DTLS::Mode mode,
        std::shared_ptr<X509> cert,
        std::shared_ptr<EVP_PKEY> key,
        const std::string &remoteFingerprint,
        bool useSRTP)
    : mState(State::UNINITIALIZED),
      mHandler(handler),
      mMode(mode),
      mRemoteFingerprint(remoteFingerprint),
      mUseSRTP(useSRTP),
      mCtx(nullptr),
      mSSL(nullptr),
      mBioR(nullptr),
      mBioW(nullptr),
      mSRTPInbound(nullptr),
      mSRTPOutbound(nullptr) {
    mCtx = SSL_CTX_new(DTLSv1_2_method());
    CHECK(mCtx);

    int result = SSL_CTX_set_cipher_list(
            mCtx, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH");

    CHECK_EQ(result, 1);

    SSL_CTX_set_verify(
            mCtx,
            SSL_VERIFY_PEER
                | SSL_VERIFY_CLIENT_ONCE
                | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
            &DTLS::OnVerifyPeerCertificate);

    CHECK(useCertificate(cert));
    CHECK(usePrivateKey(key));

    if (mUseSRTP) {
        result = SSL_CTX_set_tlsext_use_srtp(mCtx, "SRTP_AES128_CM_SHA1_80");
        CHECK_EQ(result, 0);
    }

    mSSL = SSL_new(mCtx);
    CHECK(mSSL);

    SSL_set_ex_data(mSSL, gDTLSInstanceIndex, this);

    mBioR = BIO_new(BIO_s_mem());
    CHECK(mBioR);

    mBioW = BIO_new(BIO_s_mem());
    CHECK(mBioW);

    SSL_set_bio(mSSL, mBioR, mBioW);

    if (mode == Mode::CONNECT) {
        SSL_set_connect_state(mSSL);
    } else {
        SSL_set_accept_state(mSSL);
    }
}

DTLS::~DTLS() {
    if (mSRTPOutbound) {
        srtp_dealloc(mSRTPOutbound);
        mSRTPOutbound = nullptr;
    }

    if (mSRTPInbound) {
        srtp_dealloc(mSRTPInbound);
        mSRTPInbound = nullptr;
    }

    if (mSSL) {
        SSL_shutdown(mSSL);
    }

    SSL_free(mSSL);
    mSSL = nullptr;

    mBioW = mBioR = nullptr;

    SSL_CTX_free(mCtx);
    mCtx = nullptr;
}

// static
int DTLS::OnVerifyPeerCertificate(int /* ok */, X509_STORE_CTX *ctx) {
    LOG(VERBOSE) << "OnVerifyPeerCertificate";

    SSL *ssl = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(
            ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));

    DTLS *me = static_cast<DTLS *>(SSL_get_ex_data(ssl, gDTLSInstanceIndex));

    std::unique_ptr<X509, std::function<void(X509 *)>> cert(
            SSL_get_peer_certificate(ssl), X509_free);

    if (!cert) {
        LOG(ERROR) << "SSLSocket::isPeerCertificateValid no certificate.";

        return 0;
    }

    auto spacePos = me->mRemoteFingerprint.find(' ');
    CHECK(spacePos != std::string::npos);
    auto digestName = me->mRemoteFingerprint.substr(0, spacePos);
    CHECK(!strcasecmp(digestName.c_str(), "sha-256"));

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

    LOG(VERBOSE)
        << "Client offered certificate w/ fingerprint "
        << ss.str();

    LOG(VERBOSE) << "should be: " << me->mRemoteFingerprint;

    auto remoteFingerprintHash = me->mRemoteFingerprint.substr(spacePos + 1);
    bool match = !strcasecmp(remoteFingerprintHash.c_str(), ss.str().c_str());

    if (!match) {
        LOG(ERROR)
            << "The peer's certificate's fingerprint does not match that "
            << "published in the SDP!";
    }

    return match;
}

void DTLS::connect(const sockaddr_storage &remoteAddr) {
    CHECK_EQ(static_cast<int>(mState), static_cast<int>(State::UNINITIALIZED));

    mRemoteAddr = remoteAddr;
    mState = State::CONNECTING;

    tryConnecting();
}

void DTLS::doTheThing(int res) {
    LOG(VERBOSE) << "doTheThing(" << res << ")";

    int err = SSL_get_error(mSSL, res);

    switch (err) {
        case SSL_ERROR_WANT_READ:
        {
            LOG(VERBOSE) << "SSL_ERROR_WANT_READ";

            queueOutputDataFromDTLS();
            break;
        }

        case SSL_ERROR_WANT_WRITE:
        {
            LOG(VERBOSE) << "SSL_ERROR_WANT_WRITE";
            break;
        }

        case SSL_ERROR_NONE:
        {
            LOG(VERBOSE) << "SSL_ERROR_NONE";
            break;
        }

        case SSL_ERROR_SYSCALL:
        default:
        {
            LOG(ERROR)
                << "DTLS stack returned error "
                << err
                << " ("
                << SSL_state_string_long(mSSL)
                << ")";
        }
    }
}

void DTLS::queueOutputDataFromDTLS() {
    auto handler = mHandler.lock();

    if (!handler) {
        return;
    }

    int n;

    do {
        char buf[RTPSocketHandler::kMaxUDPPayloadSize];
        n = BIO_read(mBioW, buf, sizeof(buf));

        if (n > 0) {
            LOG(VERBOSE) << "queueing " << n << " bytes of output data from DTLS.";

            handler->queueDatagram(
                    mRemoteAddr, buf, static_cast<size_t>(n));
        } else if (BIO_should_retry(mBioW)) {
            continue;
        } else {
            CHECK(!"Should not be here");
        }
    } while (n > 0);
}

void DTLS::tryConnecting() {
    CHECK_EQ(static_cast<int>(mState), static_cast<int>(State::CONNECTING));

    int res =
        (mMode == Mode::CONNECT)
            ? SSL_connect(mSSL) : SSL_accept(mSSL);

    if (res != 1) {
        doTheThing(res);
    } else {
        queueOutputDataFromDTLS();

        LOG(INFO) << "DTLS connection established.";
        mState = State::CONNECTED;

        auto handler = mHandler.lock();
        if (handler) {
            if (mUseSRTP) {
                getKeyingMaterial();
            }

            handler->notifyDTLSConnected();
        }
    }
}

void DTLS::inject(const uint8_t *data, size_t size) {
    LOG(VERBOSE) << "injecting " << size << " bytes into DTLS stack.";

    auto n = BIO_write(mBioR, data, size);
    CHECK_EQ(n, static_cast<int>(size));

    if (mState == State::CONNECTING) {
        if (!SSL_is_init_finished(mSSL)) {
            tryConnecting();
        }
    }
}

void DTLS::getKeyingMaterial() {
    static constexpr char kLabel[] = "EXTRACTOR-dtls_srtp";

    // These correspond to the chosen option SRTP_AES128_CM_SHA1_80, passed
    // to SSL_CTX_set_tlsext_use_srtp before. c/f RFC 5764 4.1.2

    uint8_t material[(SRTP_AES_128_KEY_LEN + SRTP_SALT_LEN) * 2];

    auto res = SSL_export_keying_material(
            mSSL,
            material,
            sizeof(material),
            kLabel,
            strlen(kLabel),
            nullptr /* context */,
            0 /* contextlen */,
            0 /* use_context */);

    CHECK_EQ(res, 1);

    LOG(VERBOSE) << "keying material:";
    LOG(VERBOSE) << hexdump(material, sizeof(material));

    size_t offset = 0;
    const uint8_t *clientKey = &material[offset];
    offset += SRTP_AES_128_KEY_LEN;
    const uint8_t *serverKey = &material[offset];
    offset += SRTP_AES_128_KEY_LEN;
    const uint8_t *clientSalt = &material[offset];
    offset += SRTP_SALT_LEN;
    const uint8_t *serverSalt = &material[offset];
    offset += SRTP_SALT_LEN;

    CHECK_EQ(offset, sizeof(material));

    std::string sendKey(
            reinterpret_cast<const char *>(clientKey), SRTP_AES_128_KEY_LEN);

    sendKey.append(
            reinterpret_cast<const char *>(clientSalt), SRTP_SALT_LEN);

    std::string receiveKey(
            reinterpret_cast<const char *>(serverKey), SRTP_AES_128_KEY_LEN);

    receiveKey.append(
            reinterpret_cast<const char *>(serverSalt), SRTP_SALT_LEN);

    if (mMode == Mode::CONNECT) {
        CreateSRTPSession(&mSRTPInbound, receiveKey, ssrc_any_inbound);
        CreateSRTPSession(&mSRTPOutbound, sendKey, ssrc_any_outbound);
    } else {
        CreateSRTPSession(&mSRTPInbound, sendKey, ssrc_any_inbound);
        CreateSRTPSession(&mSRTPOutbound, receiveKey, ssrc_any_outbound);
    }
}

// static
void DTLS::CreateSRTPSession(
        srtp_t *session,
        const std::string &keyAndSalt,
        srtp_ssrc_type_t direction) {
    srtp_policy_t policy;
    memset(&policy, 0, sizeof(policy));

    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);

    policy.ssrc.type = direction;
    policy.ssrc.value = 0;

    policy.key =
        const_cast<unsigned char *>(
                reinterpret_cast<const unsigned char *>(keyAndSalt.c_str()));

    policy.allow_repeat_tx = 1;
    policy.next = nullptr;

    auto ret = srtp_create(session, &policy);
    CHECK_EQ(ret, srtp_err_status_ok);
}

size_t DTLS::protect(void *data, size_t size, bool isRTP) {
    int len = static_cast<int>(size);

    auto ret =
        isRTP
            ? srtp_protect(mSRTPOutbound, data, &len)
            : srtp_protect_rtcp(mSRTPOutbound, data, &len);

    CHECK_EQ(ret, srtp_err_status_ok);

    return static_cast<size_t>(len);
}

size_t DTLS::unprotect(void *data, size_t size, bool isRTP) {
    int len = static_cast<int>(size);

    auto ret =
        isRTP
            ? srtp_unprotect(mSRTPInbound, data, &len)
            : srtp_unprotect_rtcp(mSRTPInbound, data, &len);

    if (ret == srtp_err_status_replay_fail) {
        LOG(WARNING)
            << "srtp_unprotect"
            << (isRTP ? "" : "_rtcp")
            << " returned srtp_err_status_replay_fail, ignoring packet.";

        return 0;
    }

    CHECK_EQ(ret, srtp_err_status_ok);

    return static_cast<size_t>(len);
}

ssize_t DTLS::readApplicationData(void *data, size_t size) {
    auto res = SSL_read(mSSL, data, size);

    if (res < 0) {
        doTheThing(res);
        return -1;
    }

    return res;
}

ssize_t DTLS::writeApplicationData(const void *data, size_t size) {
    auto res = SSL_write(mSSL, data, size);

    queueOutputDataFromDTLS();

    // May have to queue the data and "doTheThing" on failure...
    CHECK_EQ(res, static_cast<int>(size));

    return res;
}
