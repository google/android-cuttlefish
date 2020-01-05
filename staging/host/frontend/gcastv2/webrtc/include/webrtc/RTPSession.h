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
