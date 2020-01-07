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

#pragma once

#include <https/RunLoop.h>

#include <memory>
#include <optional>
#include <string_view>

#include <netinet/in.h>
#include <openssl/ssl.h>

struct RTPSession : std::enable_shared_from_this<RTPSession> {
    explicit RTPSession(
            std::string_view localUFrag,
            std::string_view localPassword,
            std::shared_ptr<X509> localCertificate,
            std::shared_ptr<EVP_PKEY> localKey);

    bool isActive() const;
    void setIsActive();

    void setRemoteParams(
            std::string_view remoteUFrag,
            std::string_view remotePassword,
            std::string_view remoteFingerprint);

    std::string localUFrag() const;
    std::string localPassword() const;
    std::shared_ptr<X509> localCertificate() const;
    std::shared_ptr<EVP_PKEY> localKey() const;
    std::string localFingerprint() const;

    std::string remoteUFrag() const;
    std::string remotePassword() const;
    std::string remoteFingerprint() const;

    bool hasRemoteAddress() const;
    sockaddr_storage remoteAddress() const;
    void setRemoteAddress(const sockaddr_storage &remoteAddr);

    void schedulePing(
            std::shared_ptr<RunLoop> runLoop,
            RunLoop::AsyncFunction cb,
            std::chrono::steady_clock::duration delay);

private:
    std::string mLocalUFrag;
    std::string mLocalPassword;
    std::shared_ptr<X509> mLocalCertificate;
    std::shared_ptr<EVP_PKEY> mLocalKey;

    std::optional<std::string> mRemoteUFrag;
    std::optional<std::string> mRemotePassword;
    std::optional<std::string> mRemoteFingerprint;
    std::optional<sockaddr_storage> mRemoteAddr;

    RunLoop::Token mPingToken;

    bool mIsActive;
};
