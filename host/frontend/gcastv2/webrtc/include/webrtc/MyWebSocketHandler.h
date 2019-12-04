#pragma once

#include <webrtc/RTPSession.h>
#include <webrtc/RTPSocketHandler.h>
#include <webrtc/SDP.h>
#include <webrtc/ServerState.h>

#include <https/WebSocketHandler.h>
#include <https/RunLoop.h>
#include <media/stagefright/foundation/JSONObject.h>
#include <source/StreamingSink.h>

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

struct MyWebSocketHandler
    : public WebSocketHandler,
      public std::enable_shared_from_this<MyWebSocketHandler> {

    explicit MyWebSocketHandler(
            std::shared_ptr<RunLoop> runLoop,
            std::shared_ptr<ServerState> serverState,
            size_t handlerId);

    ~MyWebSocketHandler() override;

    int handleMessage(
            uint8_t headerByte, const uint8_t *msg, size_t len) override;

private:
    using JSONObject = android::JSONObject;

    enum OptionBits : uint32_t {
        disableAudio                        = 1,
        bundleTracks                        = 2,
        enableData                          = 4,
        useSingleCertificateForAllTracks    = 8,
    };

    using StreamingSink = android::StreamingSink;

    std::shared_ptr<RunLoop> mRunLoop;
    std::shared_ptr<ServerState> mServerState;
    size_t mId;
    uint32_t mOptions;

    // Vector has the same ordering as the media entries in the SDP, i.e.
    // vector index is "mlineIndex". (unless we are bundling, in which case
    // there is only a single session).
    std::vector<std::shared_ptr<RTPSession>> mSessions;

    SDP mOfferedSDP;
    std::vector<std::shared_ptr<RTPSocketHandler>> mRTPs;

    std::shared_ptr<StreamingSink> mTouchSink;

    std::pair<std::shared_ptr<X509>, std::shared_ptr<EVP_PKEY>>
        mCertificateAndKey;

    // Pass -1 for mlineIndex to access the "general" section.
    std::optional<std::string> getSDPValue(
            ssize_t mlineIndex,
            std::string_view key,
            bool fallthroughToGeneralSection) const;

    std::string getRemotePassword(size_t mlineIndex) const;
    std::string getRemoteUFrag(size_t mlineIndex) const;
    std::string getRemoteFingerprint(size_t mlineIndex) const;

    bool getCandidate(int32_t mid);

    static std::pair<std::shared_ptr<X509>, std::shared_ptr<EVP_PKEY>>
        CreateDTLSCertificateAndKey();

    std::pair<std::string, std::string> createUniqueUFragAndPassword();

    void parseOptions(const std::string &pathAndQuery);
    size_t countTracks() const;

    void prepareSessions();

    void emitTrackIceOptionsAndFingerprint(
            std::stringstream &ss, size_t mlineIndex) const;

    // Returns -1 on error.
    ssize_t mlineIndexForMid(int32_t mid) const;

    static void CreateRandomIceCharSequence(char *dst, size_t size);
};


