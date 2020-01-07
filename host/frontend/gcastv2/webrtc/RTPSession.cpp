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

#include <webrtc/RTPSession.h>

#include <android-base/logging.h>

#include <sstream>

RTPSession::RTPSession(
        std::string_view localUFrag,
        std::string_view localPassword,
        std::shared_ptr<X509> localCertificate,
        std::shared_ptr<EVP_PKEY> localKey)
    : mLocalUFrag(localUFrag),
      mLocalPassword(localPassword),
      mLocalCertificate(localCertificate),
      mLocalKey(localKey),
      mPingToken(0),
      mIsActive(false) {
}

bool RTPSession::isActive() const {
    return mIsActive;
}

void RTPSession::setIsActive() {
    mIsActive = true;
}

void RTPSession::schedulePing(
        std::shared_ptr<RunLoop> runLoop,
        RunLoop::AsyncFunction cb,
        std::chrono::steady_clock::duration delay) {
    CHECK_EQ(mPingToken, 0);

    mPingToken = runLoop->postWithDelay(
            delay,
            [weak_this = std::weak_ptr<RTPSession>(shared_from_this()),
             runLoop, cb]() {
                auto me = weak_this.lock();
                if (me) {
                    me->mPingToken = 0;
                    cb();
                }
            });
}

void RTPSession::setRemoteParams(
        std::string_view remoteUFrag,
        std::string_view remotePassword,
        std::string_view remoteFingerprint) {
    CHECK(!mRemoteUFrag && !mRemotePassword && !mRemoteFingerprint);

    mRemoteUFrag = remoteUFrag;
    mRemotePassword = remotePassword;
    mRemoteFingerprint = remoteFingerprint;
}

std::string RTPSession::localUFrag() const {
    return mLocalUFrag;
}

std::string RTPSession::localPassword() const {
    return mLocalPassword;
}

std::shared_ptr<X509> RTPSession::localCertificate() const {
    return mLocalCertificate;
}

std::shared_ptr<EVP_PKEY> RTPSession::localKey() const {
    return mLocalKey;
}

std::string RTPSession::localFingerprint() const {
    auto digest = EVP_sha256();

    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int n;
    auto res = X509_digest(mLocalCertificate.get(), digest, md, &n);
    CHECK_EQ(res, 1);

    std::stringstream ss;
    ss << "sha-256 ";
    for (unsigned int i = 0; i < n; ++i) {
        if (i > 0) {
            ss << ":";
        }

        uint8_t byte = md[i];
        uint8_t nibble = byte >> 4;
        ss << (char)(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        nibble = byte & 0xf;
        ss << (char)(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }

    return ss.str();
}

std::string RTPSession::remoteUFrag() const {
    CHECK(mRemoteUFrag.has_value());
    return *mRemoteUFrag;
}

std::string RTPSession::remotePassword() const {
    CHECK(mRemotePassword.has_value());
    return *mRemotePassword;
}

std::string RTPSession::remoteFingerprint() const {
    CHECK(mRemoteFingerprint.has_value());
    return *mRemoteFingerprint;
}

bool RTPSession::hasRemoteAddress() const {
    return mRemoteAddr.has_value();
}

sockaddr_storage RTPSession::remoteAddress() const {
    CHECK(hasRemoteAddress());
    return *mRemoteAddr;
}

void RTPSession::setRemoteAddress(const sockaddr_storage &remoteAddr) {
    CHECK(!hasRemoteAddress());
    mRemoteAddr = remoteAddr;
}

