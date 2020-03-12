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

#include <webrtc/MyWebSocketHandler.h>

#include "Utils.h"

#include <json/json.h>

#include <netdb.h>
#include <openssl/rand.h>

namespace {

// helper method to ensure a json object has the required fields convertible
// to the appropriate types.
bool validateJsonObject(
    const Json::Value &obj, const std::string &type,
    const std::vector<std::pair<std::string, Json::ValueType>> &fields,
    std::function<void(const std::string&)> onError) {
    for (const auto &field_spec : fields) {
        const auto &field_name = field_spec.first;
        auto field_type = field_spec.second;
        if (!(obj.isMember(field_name) &&
            obj[field_name].isConvertibleTo(field_type))) {
            std::string error_msg = "Expected a field named '";
            error_msg += field_name + "' of type '";
            error_msg += std::to_string(field_type);
            error_msg += "'";
            if (!type.empty()) {
                error_msg += " in message of type '" + type + "'";
            }
            error_msg += ".";
            LOG(WARNING) << error_msg;
            onError(error_msg);
            return false;
        }
    }
    return true;
}

} // namespace

MyWebSocketHandler::MyWebSocketHandler(
        std::shared_ptr<RunLoop> runLoop,
        std::shared_ptr<ServerState> serverState,
        size_t handlerId)
    : mRunLoop(runLoop),
      mServerState(serverState),
      mId(handlerId),
      mOptions(OptionBits::useSingleCertificateForAllTracks
              | OptionBits::enableData),
      mTouchSink(mServerState->getTouchSink()),
      mKeyboardSink(mServerState->getKeyboardSink()) {
}

MyWebSocketHandler::~MyWebSocketHandler() {
    mServerState->releaseHandlerId(mId);
}

int MyWebSocketHandler::handleMessage(
        uint8_t /* headerByte */, const uint8_t *msg, size_t len) {
    // android::hexdump(msg, len);

    Json::Value obj;
    Json::Reader json_reader;
    Json::FastWriter json_writer;
    auto str = reinterpret_cast<const char *>(msg);
    if (!json_reader.parse(str, str + len, obj) < 0) {
        return -EINVAL;
    }

    LOG(VERBOSE) << obj.toStyledString();

    auto sendMessageOnError =
        [this](const std::string &error_msg) {
          auto reply = "{\"error\": \"" + error_msg + "\"}";
          sendMessage(reply.c_str(), reply.size());
        };

    if (!validateJsonObject(obj, "", {{"type", Json::ValueType::stringValue}},
                            sendMessageOnError)) {
        return -EINVAL;
    }
    std::string type = obj["type"].asString();

    if (type == "greeting") {
        Json::Value reply;
        reply["type"] = "hello";
        reply["reply"] = "Right back at ya!";

        auto replyAsString = json_writer.write(reply);
        sendMessage(replyAsString.c_str(), replyAsString.size());

        if (obj.isMember("options")) {
            parseOptions(obj["options"]);
        }

        if (mOptions & OptionBits::useSingleCertificateForAllTracks) {
            mCertificateAndKey = CreateDTLSCertificateAndKey();
        }

        prepareSessions();
    } else if (type == "set-client-desc") {
        if (!validateJsonObject(obj, type,
                                {{"sdp", Json::ValueType::stringValue}},
                                sendMessageOnError)) {
            return -EINVAL;
        }

        int err = mOfferedSDP.setTo(obj["sdp"].asString());

        if (err) {
            LOG(ERROR) << "Offered SDP could not be parsed (" << err << ")";
        }

        for (size_t i = 0; i < mSessions.size(); ++i) {
            const auto &session = mSessions[i];

            session->setRemoteParams(
                    getRemoteUFrag(i),
                    getRemotePassword(i),
                    getRemoteFingerprint(i));
        }

        return err;
    } else if (type == "request-offer") {
        std::stringstream ss;

        ss <<
"v=0\r\n"
"o=- 7794515898627856655 2 IN IP4 127.0.0.1\r\n"
"s=-\r\n"
"t=0 0\r\n"
"a=msid-semantic: WMS pqWEULZNyLiJHA7lcwlUnbule9FJNk0pY0aw\r\n";

        bool bundled = false;

        if ((mOptions & OptionBits::bundleTracks) && countTracks() > 1) {
            bundled = true;

            ss << "a=group:BUNDLE 0";

            if (!(mOptions & OptionBits::disableAudio)) {
                ss << " 1";
            }

            if (mOptions & OptionBits::enableData) {
                ss << " 2";
            }

            ss << "\r\n";

            emitTrackIceOptionsAndFingerprint(ss, 0 /* mlineIndex */);
        }

        size_t mlineIndex = 0;

        // Video track (mid = 0)

        std::string videoEncodingSpecific = "a=rtpmap:96 VP8/90000\r\n";

        videoEncodingSpecific +=
"a=rtcp-fb:96 ccm fir\r\n"
"a=rtcp-fb:96 nack\r\n"
"a=rtcp-fb:96 nack pli\r\n";

        ss <<
"m=video 9 "
<< ((mOptions & OptionBits::useTCP) ? "TCP" : "UDP")
<< "/TLS/RTP/SAVPF 96 97\r\n"
"c=IN IP4 0.0.0.0\r\n"
"a=rtcp:9 IN IP4 0.0.0.0\r\n";

        if (!bundled) {
            emitTrackIceOptionsAndFingerprint(ss, mlineIndex++);
        }

        ss <<
"a=setup:actpass\r\n"
"a=mid:0\r\n"
"a=sendonly\r\n"
"a=rtcp-mux\r\n"
"a=rtcp-rsize\r\n"
"a=rtcp-xr:rcvr-rtt=all\r\n";

        ss << videoEncodingSpecific <<
"a=rtpmap:97 rtx/90000\r\n"
"a=fmtp:97 apt=96\r\n"
"a=ssrc-group:FID 3735928559 3405689008\r\n"
"a=ssrc:3735928559 cname:myWebRTP\r\n"
"a=ssrc:3735928559 msid:pqWEULZNyLiJHA7lcwlUnbule9FJNk0pY0aw 61843855-edd7-4ca9-be79-4e3ccc6cc035\r\n"
"a=ssrc:3735928559 mslabel:pqWEULZNyLiJHA7lcwlUnbule9FJNk0pY0aw\r\n"
"a=ssrc:3735928559 label:61843855-edd7-4ca9-be79-4e3ccc6cc035\r\n"
"a=ssrc:3405689008 cname:myWebRTP\r\n"
"a=ssrc:3405689008 msid:pqWEULZNyLiJHA7lcwlUnbule9FJNk0pY0aw 61843855-edd7-4ca9-be79-4e3ccc6cc035\r\n"
"a=ssrc:3405689008 mslabel:pqWEULZNyLiJHA7lcwlUnbule9FJNk0pY0aw\r\n"
"a=ssrc:3405689008 label:61843855-edd7-4ca9-be79-4e3ccc6cc035\r\n";

        if (!(mOptions & OptionBits::disableAudio)) {
            ss <<
"m=audio 9 "
<< ((mOptions & OptionBits::useTCP) ? "TCP" : "UDP")
<< "/TLS/RTP/SAVPF 98\r\n"
"c=IN IP4 0.0.0.0\r\n"
"a=rtcp:9 IN IP4 0.0.0.0\r\n";

            if (!bundled) {
                emitTrackIceOptionsAndFingerprint(ss, mlineIndex++);
            }

            ss <<
"a=setup:actpass\r\n"
"a=mid:1\r\n"
"a=sendonly\r\n"
"a=msid:pqWEULZNyLiJHA7lcwlUnbule9FJNk0pY0aw 61843856-edd7-4ca9-be79-4e3ccc6cc035\r\n"
"a=rtcp-mux\r\n"
"a=rtcp-rsize\r\n"
"a=rtpmap:98 opus/48000/2\r\n"
"a=fmtp:98 minptime=10;useinbandfec=1\r\n"
"a=ssrc-group:FID 2343432205\r\n"
"a=ssrc:2343432205 cname:myWebRTP\r\n"
"a=ssrc:2343432205 msid:pqWEULZNyLiJHA7lcwlUnbule9FJNk0pY0aw 61843856-edd7-4ca9-be79-4e3ccc6cc035\r\n"
"a=ssrc:2343432205 mslabel:pqWEULZNyLiJHA7lcwlUnbule9FJNk0pY0aw\r\n"
"a=ssrc:2343432205 label:61843856-edd7-4ca9-be79-4e3ccc6cc035\r\n";
        }

        if (mOptions & OptionBits::enableData) {
            ss <<
"m=application 9 "
<< ((mOptions & OptionBits::useTCP) ? "TCP" : "UDP")
<< "/DTLS/SCTP webrtc-datachannel\r\n"
"c=IN IP4 0.0.0.0\r\n"
"a=sctp-port:5000\r\n";

            if (!bundled) {
                emitTrackIceOptionsAndFingerprint(ss, mlineIndex++);
            }

            ss <<
"a=setup:actpass\r\n"
"a=mid:2\r\n"
"a=sendrecv\r\n"
"a=fmtp:webrtc-datachannel max-message-size=65536\r\n";
        }

        Json::Value reply;
        reply["type"] = "offer";
        reply["sdp"] = ss.str();

        auto replyAsString = json_writer.write(reply);
        sendMessage(replyAsString.c_str(), replyAsString.size());
    } else if (type == "get-ice-candidate") {
        if (!validateJsonObject(obj, type, {{"mid", Json::ValueType::intValue}},
                                sendMessageOnError)) {
            return -EINVAL;
        }
        int32_t mid = obj["mid"].asInt();

        bool success = getCandidate(mid);

        if (!success) {
            Json::Value reply;
            reply["type"] = "ice-candidate";

            auto replyAsString = json_writer.write(reply);
            sendMessage(replyAsString.c_str(), replyAsString.size());
        }
    }

    return 0;
}

size_t MyWebSocketHandler::countTracks() const {
    size_t n = 1;  // We always have a video track.

    if (!(mOptions & OptionBits::disableAudio)) {
        ++n;
    }

    if (mOptions & OptionBits::enableData) {
        ++n;
    }

    return n;
}

ssize_t MyWebSocketHandler::mlineIndexForMid(int32_t mid) const {
    switch (mid) {
        case 0:
            return 0;

        case 1:
            if (mOptions & OptionBits::disableAudio) {
                return -1;
            }

            return 1;

        case 2:
            if (!(mOptions & OptionBits::enableData)) {
                return -1;
            }

            if (mOptions & OptionBits::disableAudio) {
                return 1;
            }

            return 2;

        default:
            return -1;
    }
}

bool MyWebSocketHandler::getCandidate(int32_t mid) {
    auto mlineIndex = mlineIndexForMid(mid);

    if (mlineIndex < 0) {
        return false;
    }

    if (!(mOptions & OptionBits::bundleTracks) || mRTPs.empty()) {
        // Only allocate a local port once if we bundle tracks.

        size_t sessionIndex = mlineIndex;

        uint32_t trackMask = 0;
        if (mOptions & OptionBits::bundleTracks) {
            sessionIndex = 0;  // One session for all tracks.

            trackMask = RTPSocketHandler::TRACK_VIDEO;

            if (!(mOptions & OptionBits::disableAudio)) {
                trackMask |= RTPSocketHandler::TRACK_AUDIO;
            }

            if (mOptions & OptionBits::enableData) {
                trackMask |= RTPSocketHandler::TRACK_DATA;
            }
        } else if (mid == 0) {
            trackMask = RTPSocketHandler::TRACK_VIDEO;
        } else if (mid == 1) {
            trackMask = RTPSocketHandler::TRACK_AUDIO;
        } else {
            trackMask = RTPSocketHandler::TRACK_DATA;
        }

        const auto &session = mSessions[sessionIndex];

        auto rtp = std::make_shared<RTPSocketHandler>(
                mRunLoop,
                mServerState,
                (mOptions & OptionBits::useTCP)
                    ? RTPSocketHandler::TransportType::TCP
                    : RTPSocketHandler::TransportType::UDP,
                PF_INET,
                trackMask,
                session);

        rtp->run();

        mRTPs.push_back(rtp);
    }

    auto rtp = mRTPs.back();

    Json::Value reply;
    reply["type"] = "ice-candidate";

    auto localIPString = rtp->getLocalIPString();

    std::stringstream ss;
    ss << "candidate:0 1 ";

    if (mOptions & OptionBits::useTCP) {
        ss << "tcp";
    } else {
        ss << "UDP";
    }

    // see rfc8445, 5.1.2.1. for the derivation of "2122121471" below.
    ss << " 2122121471 " << localIPString << " " << rtp->getLocalPort() << " typ host ";

    if (mOptions & OptionBits::useTCP) {
        ss << "tcptype passive ";
    }

    ss << "generation 0 ufrag " << rtp->getLocalUFrag();

    reply["candidate"] = ss.str();
    reply["mlineIndex"] = static_cast<Json::UInt64>(mlineIndex);

    Json::FastWriter json_writer;
    auto replyAsString = json_writer.write(reply);
    sendMessage(replyAsString.c_str(), replyAsString.size());

    return true;
}

std::optional<std::string> MyWebSocketHandler::getSDPValue(
        ssize_t targetMediaIndex,
        std::string_view key,
        bool fallthroughToGeneralSection) const {

    CHECK_GE(targetMediaIndex, -1);

    if (targetMediaIndex + 1 >= mOfferedSDP.countSections()) {
        LOG(ERROR)
            << "getSDPValue: targetMediaIndex "
            << targetMediaIndex
            << " out of range (countSections()="
            << mOfferedSDP.countSections()
            << ")";

        return std::nullopt;
    }

    const std::string prefix = "a=" + std::string(key) + ":";

    auto sectionIndex = 1 + targetMediaIndex;
    auto rangeEnd = mOfferedSDP.section_end(sectionIndex);

    auto it = std::find_if(
            mOfferedSDP.section_begin(sectionIndex),
            rangeEnd,
            [prefix](const auto &line) {
        return StartsWith(line, prefix);
    });

    if (it == rangeEnd) {
        if (fallthroughToGeneralSection) {
            CHECK_NE(targetMediaIndex, -1);

            // Oh no, scary recursion ahead.
            return getSDPValue(
                    -1 /* targetMediaIndex */,
                    key,
                    false /* fallthroughToGeneralSection */);
        }

        LOG(WARNING)
            << "Unable to find '"
            << prefix
            << "' with targetMediaIndex="
            << targetMediaIndex;

        return std::nullopt;
    }

    return (*it).substr(prefix.size());
}

std::string MyWebSocketHandler::getRemotePassword(size_t mlineIndex) const {
    auto value = getSDPValue(
            mlineIndex, "ice-pwd", true /* fallthroughToGeneralSection */);

    return value ? *value : std::string();
}

std::string MyWebSocketHandler::getRemoteUFrag(size_t mlineIndex) const {
    auto value = getSDPValue(
            mlineIndex, "ice-ufrag", true /* fallthroughToGeneralSection */);

    return value ? *value : std::string();
}

std::string MyWebSocketHandler::getRemoteFingerprint(size_t mlineIndex) const {
    auto value = getSDPValue(
            mlineIndex, "fingerprint", true /* fallthroughToGeneralSection */);

    return value ? *value : std::string();
}

// static
std::pair<std::shared_ptr<X509>, std::shared_ptr<EVP_PKEY>>
MyWebSocketHandler::CreateDTLSCertificateAndKey() {
    // Modeled after "https://stackoverflow.com/questions/256405/
    // programmatically-create-x509-certificate-using-openssl".

    std::shared_ptr<EVP_PKEY> pkey(EVP_PKEY_new(), EVP_PKEY_free);

    std::unique_ptr<RSA, std::function<void(RSA *)>> rsa(
            RSA_new(), RSA_free);

    BIGNUM exponent;
    BN_init(&exponent);
    BN_set_word(&exponent, RSA_F4);

    int res = RSA_generate_key_ex(
            rsa.get() /* rsa */, 2048, &exponent, nullptr /* callback */);

    CHECK_EQ(res, 1);

    EVP_PKEY_assign_RSA(pkey.get(), rsa.release());

    std::shared_ptr<X509> x509(X509_new(), X509_free);

    ASN1_INTEGER_set(X509_get_serialNumber(x509.get()), 1);

    X509_gmtime_adj(X509_get_notBefore(x509.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(x509.get()), 60 * 60 * 24 * 7); // 7 days.

    X509_set_pubkey(x509.get(), pkey.get());

    X509_NAME *name = X509_get_subject_name(x509.get());

    X509_NAME_add_entry_by_txt(
            name, "C",  MBSTRING_ASC, (unsigned char *)"US", -1, -1, 0);

    X509_NAME_add_entry_by_txt(
            name,
            "O",
            MBSTRING_ASC,
            (unsigned char *)"Beyond Aggravated",
            -1,
            -1,
            0);

    X509_NAME_add_entry_by_txt(
            name, "CN", MBSTRING_ASC, (unsigned char *)"localhost", -1, -1, 0);

    X509_set_issuer_name(x509.get(), name);

    auto digest = EVP_sha256();

    X509_sign(x509.get(), pkey.get(), digest);

    return std::make_pair(x509, pkey);
}

void MyWebSocketHandler::parseOptions(const Json::Value& options) {
    if (options.isMember("disable_audio") && options["disable_audio"].isBool()) {
        auto mask = OptionBits::disableAudio;
        mOptions = (mOptions & ~mask) | (options["disable_audio"].asBool() ? mask : 0);
    }
    if (options.isMember("bundle_tracks") && options["bundle_tracks"].isBool()) {
        auto mask = OptionBits::bundleTracks;
        mOptions = (mOptions & ~mask) | (options["bundle_tracks"].asBool() ? mask : 0);
    }
    if (options.isMember("enable_data") && options["enable_data"].isBool()) {
        auto mask = OptionBits::enableData;
        mOptions = (mOptions & ~mask) | (options["enable_data"].asBool() ? mask : 0);
    }
    if (options.isMember("use_tcp") && options["use_tcp"].isBool()) {
        auto mask = OptionBits::useTCP;
        mOptions = (mOptions & ~mask) | (options["use_tcp"].asBool() ? mask : 0);
    }
}

// static
void MyWebSocketHandler::CreateRandomIceCharSequence(char *dst, size_t size) {
    // Per RFC 5245 an ice-char is alphanumeric, '+' or '/', i.e. 64 distinct
    // character values (6 bit).

    CHECK_EQ(1, RAND_bytes(reinterpret_cast<unsigned char *>(dst), size));

    for (size_t i = 0; i < size; ++i) {
        char x = dst[i] & 0x3f;
        if (x < 26) {
            x += 'a';
        } else if (x < 52) {
            x += 'A' - 26;
        } else if (x < 62) {
            x += '0' - 52;
        } else if (x < 63) {
            x = '+';
        } else {
            x = '/';
        }

        dst[i] = x;
    }
}

std::pair<std::string, std::string>
MyWebSocketHandler::createUniqueUFragAndPassword() {
    // RFC 5245, section 15.4 mandates that uFrag is at least 4 and password
    // at least 22 ice-chars long.

    char uFragChars[4];

    for (;;) {
        CreateRandomIceCharSequence(uFragChars, sizeof(uFragChars));

        std::string uFrag(uFragChars, sizeof(uFragChars));

        auto it = std::find_if(
                mSessions.begin(), mSessions.end(),
                [uFrag](const auto &session) {
                    return session->localUFrag() == uFrag;
                });

        if (it == mSessions.end()) {
            // This uFrag is not in use yet.
            break;
        }
    }

    char passwordChars[22];
    CreateRandomIceCharSequence(passwordChars, sizeof(passwordChars));

    return std::make_pair(
            std::string(uFragChars, sizeof(uFragChars)),
            std::string(passwordChars, sizeof(passwordChars)));
}

void MyWebSocketHandler::prepareSessions() {
    size_t numSessions =
        (mOptions & OptionBits::bundleTracks) ? 1 : countTracks();

    for (size_t i = 0; i < numSessions; ++i) {
        auto [ufrag, password] = createUniqueUFragAndPassword();

        auto [certificate, key] =
            (mOptions & OptionBits::useSingleCertificateForAllTracks)
                ? mCertificateAndKey : CreateDTLSCertificateAndKey();

        mSessions.push_back(
                std::make_shared<RTPSession>(
                    ufrag, password, certificate, key));
    }
}

void MyWebSocketHandler::emitTrackIceOptionsAndFingerprint(
        std::stringstream &ss, size_t mlineIndex) const {
    CHECK_LT(mlineIndex, mSessions.size());
    const auto &session = mSessions[mlineIndex];

    ss << "a=ice-ufrag:" << session->localUFrag() << "\r\n";
    ss << "a=ice-pwd:" << session->localPassword() << "\r\n";
    ss << "a=ice-options:trickle\r\n";
    ss << "a=fingerprint:" << session->localFingerprint() << "\r\n";
}
