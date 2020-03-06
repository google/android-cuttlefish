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

#include <webrtc/RTPSocketHandler.h>

#include <webrtc/Keyboard.h>
#include <webrtc/MyWebSocketHandler.h>
#include <webrtc/STUNMessage.h>
#include <Utils.h>

#include <https/PlainSocket.h>
#include <https/SafeCallbackable.h>
#include <https/Support.h>
#include <android-base/logging.h>

#include <netdb.h>
#include <netinet/in.h>

#include <cstring>
#include <iostream>
#include <set>

#include <json/json.h>

#include <gflags/gflags.h>

DECLARE_string(public_ip);

// These are the ports we currently open in the firewall (15550..15557)
static constexpr int kPortRangeBegin = 15550;
static constexpr int kPortRangeEnd = 15558;

static socklen_t getSockAddrLen(const sockaddr_storage &addr) {
    switch (addr.ss_family) {
        case AF_INET:
            return sizeof(sockaddr_in);
        case AF_INET6:
            return sizeof(sockaddr_in6);
        default:
            CHECK(!"Should not be here.");
            return 0;
    }
}

static int acquirePort(int sockfd, int domain) {
    sockaddr_storage addr;
    uint16_t* port_ptr;

    if (domain == PF_INET) {
        sockaddr_in addrV4;
        memset(addrV4.sin_zero, 0, sizeof(addrV4.sin_zero));
        addrV4.sin_family = AF_INET;
        addrV4.sin_addr.s_addr = INADDR_ANY;
        memcpy(&addr, &addrV4, sizeof(addrV4));
        port_ptr = &(reinterpret_cast<sockaddr_in*>(&addr)->sin_port);
    } else {
        CHECK_EQ(domain, PF_INET6);
        sockaddr_in6 addrV6;
        addrV6.sin6_family = AF_INET6;
        addrV6.sin6_addr = in6addr_any;
        addrV6.sin6_scope_id = 0;
        memcpy(&addr, &addrV6, sizeof(addrV6));
        port_ptr = &(reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port);
    }

    int port = kPortRangeBegin;
    for (;port < kPortRangeEnd; ++port) {
        *port_ptr = htons(port);
        errno = 0;
        int res = bind(sockfd, reinterpret_cast<const sockaddr *>(&addr),
                       getSockAddrLen(addr));
        if (res == 0) {
            return port;
        }
        if (errno != EADDRINUSE) {
            return -1;
        }
        // else try the next port
    }

    return -1;
}

static void ProcessInputEvent(std::shared_ptr<ServerState> server_state,
                              const uint8_t* msg, size_t size) {
    // TODO(jemoreira) consider binary protocol to avoid JSON parsing overhead
    Json::Value evt;
    Json::Reader json_reader;
    auto str = reinterpret_cast<const char *>(msg);
    if (!json_reader.parse(str, str + size, evt) < 0) {
        LOG(ERROR) << "Received invalid JSON object in input channel:";
        LOG(INFO) << hexdump(msg, size);
        return;
    }
    if (!evt.isMember("type") || !evt["type"].isString()) {
        LOG(ERROR) << "Input event doesn't have a valid 'type' field";
        return;
    }
    auto event_type = evt["type"].asString();
    if (event_type == "mouse") {
        if (!evt.isMember("down") || !evt["down"].isInt()) {
            LOG(ERROR) << "Integer field 'down' is required for events of type "
                       << event_type;
            return;
        }
        if (!evt.isMember("x") || !evt["x"].isInt()) {
            LOG(ERROR) << "Integer field 'x' is required for events of type "
                       << event_type;
            return;
        }
        if (!evt.isMember("y") || !evt["y"].isInt()) {
            LOG(ERROR) << "Integer field 'y' is required for events of type "
                       << event_type;
            return;
        }
        int32_t down = evt["down"].asInt();
        int32_t x = evt["x"].asInt();
        int32_t y = evt["y"].asInt();

        server_state->getTouchSink()->injectTouchEvent(x, y, down != 0);
    } else if (event_type == "multi-touch") {
        if (!evt.isMember("id") || !evt["id"].isInt()) {
            LOG(ERROR) << "Integer field 'id' is required for events of type "
                       << event_type;
            return;
        }
        if (!evt.isMember("initialDown") || !evt["initialDown"].isInt()) {
            LOG(ERROR) << "Integer field 'initialDown' is required for events "
                       << "of type " << event_type;
            return;
        }
        if (!evt.isMember("x") || !evt["x"].isInt()) {
            LOG(ERROR) << "Integer field 'x' is required for events of type "
                       << event_type;
            return;
        }
        if (!evt.isMember("y") || !evt["y"].isInt()) {
            LOG(ERROR) << "Integer field 'y' is required for events of type "
                       << event_type;
            return;
        }
        if (!evt.isMember("slot") || !evt["slot"].isInt()) {
            LOG(ERROR) << "Integer field 'slot' is required for events of type "
                       << event_type;
            return;
        }
        int32_t id = evt["id"].asInt();
        int32_t initialDown = evt["initialDown"].asInt();
        int32_t x = evt["x"].asInt();
        int32_t y = evt["y"].asInt();
        int32_t slot = evt["slot"].asInt();

        server_state->getTouchSink()->injectMultiTouchEvent(id, slot, x, y,
                                                            initialDown);
    } else if (event_type == "keyboard") {
        if (!evt.isMember("event_type") || !evt["event_type"].isString()) {
            LOG(ERROR) << "String field 'event_type' is required for events of "
                       << "type " << event_type;
            return;
        }
        if (!evt.isMember("keycode") || !evt["keycode"].isString()) {
            LOG(ERROR) << "String field 'keycode' is required for events of "
                       << "type " << event_type;
            return;
        }
        auto down = evt["event_type"].asString() == std::string("keydown");
        auto code = DomKeyCodeToLinux(evt["keycode"].asString());
        server_state->getKeyboardSink()->injectEvent(down, code);
    } else {
        LOG(ERROR) << "Unrecognized event type: " << event_type;
        return;
    }
}

RTPSocketHandler::RTPSocketHandler(
        std::shared_ptr<RunLoop> runLoop,
        std::shared_ptr<ServerState> serverState,
        int domain,
        uint32_t trackMask,
        std::shared_ptr<RTPSession> session)
    : mRunLoop(runLoop),
      mServerState(serverState),
      mTrackMask(trackMask),
      mSession(session),
      mSendPending(false),
      mDTLSConnected(false) {
    int sock = socket(domain, SOCK_DGRAM, 0);

    makeFdNonblocking(sock);
    mSocket = std::make_shared<PlainSocket>(mRunLoop, sock);

    mLocalPort = acquirePort(sock, domain);

    CHECK(mLocalPort > 0);

    auto videoPacketizer =
        (trackMask & TRACK_VIDEO)
            ? mServerState->getVideoPacketizer() : nullptr;

    auto audioPacketizer =
        (trackMask & TRACK_AUDIO)
            ? mServerState->getAudioPacketizer() : nullptr;

    mRTPSender = std::make_shared<RTPSender>(
            mRunLoop,
            this,
            videoPacketizer,
            audioPacketizer);

    if (trackMask & TRACK_VIDEO) {
        mRTPSender->addSource(0xdeadbeef);
        mRTPSender->addSource(0xcafeb0b0);

        mRTPSender->addRetransInfo(0xdeadbeef, 96, 0xcafeb0b0, 97);
    }

    if (trackMask & TRACK_AUDIO) {
        mRTPSender->addSource(0x8badf00d);
    }
}

uint16_t RTPSocketHandler::getLocalPort() const {
    return mLocalPort;
}

std::string RTPSocketHandler::getLocalUFrag() const {
    return mSession->localUFrag();
}

std::string RTPSocketHandler::getLocalIPString() const {
    return FLAGS_public_ip;
}

void RTPSocketHandler::run() {
    mSocket->postRecv(makeSafeCallback(this, &RTPSocketHandler::onReceive));
}

void RTPSocketHandler::onReceive() {
    std::vector<uint8_t> buffer(kMaxUDPPayloadSize);

    uint8_t *data = buffer.data();

    sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);

    auto n = mSocket->recvfrom(
            data, buffer.size(), reinterpret_cast<sockaddr *>(&addr), &addrLen);

    STUNMessage msg(data, n);
    if (!msg.isValid()) {
        if (mDTLSConnected) {
            int err = -EINVAL;
            if (mRTPSender) {
                err = onSRTPReceive(data, static_cast<size_t>(n));
            }

            if (err == -EINVAL) {
                LOG(VERBOSE) << "Sending to DTLS instead:";
                LOG(VERBOSE) << hexdump(data, n);

                onDTLSReceive(data, static_cast<size_t>(n));

                if (mTrackMask & TRACK_DATA) {
                    ssize_t n;

                    do {
                        uint8_t buf[kMaxUDPPayloadSize];
                        n = mDTLS->readApplicationData(buf, sizeof(buf));

                        if (n > 0) {
                            auto err = mSCTPHandler->inject(
                                    buf, static_cast<size_t>(n));

                            if (err) {
                                LOG(WARNING)
                                    << "SCTPHandler::inject returned error "
                                    << err;
                            }
                        }
                    } while (n > 0);
                }
            }
        } else {
            onDTLSReceive(data, static_cast<size_t>(n));
        }

        run();
        return;
    }

    if (msg.type() == 0x0001 /* Binding Request */) {
        STUNMessage response(0x0101 /* Binding Response */, msg.data() + 8);

        if (!matchesSession(msg)) {
            LOG(WARNING) << "Unknown session or no USERNAME.";
            run();
            return;
        }

        const auto &answerPassword = mSession->localPassword();

        // msg.dump(answerPassword);

        if (addr.ss_family == AF_INET) {
            uint8_t attr[8];
            attr[0] = 0x00;

            sockaddr_in addrV4;
            CHECK_EQ(addrLen, sizeof(addrV4));

            memcpy(&addrV4, &addr, addrLen);

            attr[1] = 0x01;  // IPv4

            static constexpr uint32_t kMagicCookie = 0x2112a442;

            uint16_t portHost = ntohs(addrV4.sin_port);
            portHost ^= (kMagicCookie >> 16);

            uint32_t ipHost = ntohl(addrV4.sin_addr.s_addr);
            ipHost ^= kMagicCookie;

            attr[2] = portHost >> 8;
            attr[3] = portHost & 0xff;
            attr[4] = ipHost >> 24;
            attr[5] = (ipHost >> 16) & 0xff;
            attr[6] = (ipHost >> 8) & 0xff;
            attr[7] = ipHost & 0xff;

            response.addAttribute(
                    0x0020 /* XOR-MAPPED-ADDRESS */, attr, sizeof(attr));
        } else {
            uint8_t attr[20];
            attr[0] = 0x00;

            CHECK_EQ(addr.ss_family, AF_INET6);

            sockaddr_in6 addrV6;
            CHECK_EQ(addrLen, sizeof(addrV6));

            memcpy(&addrV6, &addr, addrLen);

            attr[1] = 0x02;  // IPv6

            static constexpr uint32_t kMagicCookie = 0x2112a442;

            uint16_t portHost = ntohs(addrV6.sin6_port);
            portHost ^= (kMagicCookie >> 16);

            attr[2] = portHost >> 8;
            attr[3] = portHost & 0xff;

            uint8_t ipHost[16];

            std::string out;

            for (size_t i = 0; i < 16; ++i) {
                ipHost[i] = addrV6.sin6_addr.s6_addr[15 - i];

                if (!out.empty()) {
                    out += ":";
                }
                out += StringPrintf("%02x", ipHost[i]);

                ipHost[i] ^= response.data()[4 + i];
            }

            LOG(VERBOSE) << "IP6 = " << out;

            for (size_t i = 0; i < 16; ++i) {
                attr[4 + i] = ipHost[15 - i];
            }

            response.addAttribute(
                    0x0020 /* XOR-MAPPED-ADDRESS */, attr, sizeof(attr));
        }

        response.addMessageIntegrityAttribute(answerPassword);
        response.addFingerprint();

        // response.dump(answerPassword);

        auto res =
            mSocket->sendto(
                    response.data(),
                    response.size(),
                    reinterpret_cast<const sockaddr *>(&addr),
                    addrLen);

        CHECK_GT(res, 0);
        CHECK_EQ(static_cast<size_t>(res), response.size());

        if (!mSession->isActive()) {
            mSession->setRemoteAddress(addr);

            mSession->setIsActive();

            mSession->schedulePing(
                    mRunLoop,
                    makeSafeCallback(
                        this, &RTPSocketHandler::pingRemote, mSession),
                    std::chrono::seconds(0));
        }

    } else {
        // msg.dump();

        if (msg.type() == 0x0101 && !mDTLS) {
            mDTLS = std::make_shared<DTLS>(
                    shared_from_this(),
                    DTLS::Mode::ACCEPT,
                    mSession->localCertificate(),
                    mSession->localKey(),
                    mSession->remoteFingerprint(),
                    (mTrackMask != TRACK_DATA) /* useSRTP */);

            mDTLS->connect(mSession->remoteAddress());
        }
    }

    run();
}

bool RTPSocketHandler::matchesSession(const STUNMessage &msg) const {
    const void *attrData;
    size_t attrSize;
    if (!msg.findAttribute(0x0006 /* USERNAME */, &attrData, &attrSize)) {
        return false;
    }

    std::string uFragPair(static_cast<const char *>(attrData), attrSize);
    auto colonPos = uFragPair.find(':');

    if (colonPos == std::string::npos) {
        return false;
    }

    std::string localUFrag(uFragPair, 0, colonPos);
    std::string remoteUFrag(uFragPair, colonPos + 1);

    if (mSession->localUFrag() != localUFrag
            || mSession->remoteUFrag() != remoteUFrag) {

        LOG(WARNING)
            << "Unable to find session localUFrag='"
            << localUFrag
            << "', remoteUFrag='"
            << remoteUFrag
            << "'";

        return false;
    }

    return true;
}

void RTPSocketHandler::pingRemote(std::shared_ptr<RTPSession> session) {
    std::vector<uint8_t> transactionID { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

    STUNMessage msg(
            0x0001 /* Binding Request */,
            transactionID.data());

    std::string uFragPair =
            session->remoteUFrag() + ":" + session->localUFrag();

    msg.addAttribute(
            0x0006 /* USERNAME */,
            uFragPair.c_str(),
            uFragPair.size());

    uint64_t tieBreaker = 0xdeadbeefcafeb0b0;  // XXX
    msg.addAttribute(
            0x802a /* ICE-CONTROLLING */,
            &tieBreaker,
            sizeof(tieBreaker));

    uint32_t priority = 0xdeadbeef;
    msg.addAttribute(
            0x0024 /* PRIORITY */, &priority, sizeof(priority));

    // We're the controlling agent and including the "USE-CANDIDATE" attribute
    // below nominates this candidate.
    msg.addAttribute(0x0025 /* USE_CANDIDATE */);

    msg.addMessageIntegrityAttribute(session->remotePassword());
    msg.addFingerprint();

    queueDatagram(session->remoteAddress(), msg.data(), msg.size());

    session->schedulePing(
            mRunLoop,
            makeSafeCallback(this, &RTPSocketHandler::pingRemote, session),
            std::chrono::seconds(1));
}

RTPSocketHandler::Datagram::Datagram(
        const sockaddr_storage &addr, const void *data, size_t size)
    : mData(size),
      mAddr(addr) {
    memcpy(mData.data(), data, size);
}

const void *RTPSocketHandler::Datagram::data() const {
    return mData.data();
}

size_t RTPSocketHandler::Datagram::size() const {
    return mData.size();
}

const sockaddr_storage &RTPSocketHandler::Datagram::remoteAddress() const {
    return mAddr;
}

void RTPSocketHandler::queueDatagram(
        const sockaddr_storage &addr, const void *data, size_t size) {
    auto datagram = std::make_shared<Datagram>(addr, data, size);

    CHECK_LE(size, RTPSocketHandler::kMaxUDPPayloadSize);

    mRunLoop->post(
            makeSafeCallback<RTPSocketHandler>(
                this,
                [datagram](RTPSocketHandler *me) {
                    me->mOutQueue.push_back(datagram);

                    if (!me->mSendPending) {
                        me->scheduleDrainOutQueue();
                    }
                }));
}

void RTPSocketHandler::scheduleDrainOutQueue() {
    CHECK(!mSendPending);

    mSendPending = true;
    mSocket->postSend(
            makeSafeCallback(
                this, &RTPSocketHandler::drainOutQueue));
}

void RTPSocketHandler::drainOutQueue() {
    mSendPending = false;

    CHECK(!mOutQueue.empty());

    do {
        auto datagram = mOutQueue.front();

        ssize_t n;
        do {
            const sockaddr_storage &remoteAddr = datagram->remoteAddress();

            n = mSocket->sendto(
                    datagram->data(),
                    datagram->size(),
                    reinterpret_cast<const sockaddr *>(&remoteAddr),
                    getSockAddrLen(remoteAddr));
        } while (n < 0 && errno == EINTR);

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            CHECK(!"Should not be here");
        }

        mOutQueue.pop_front();

    } while (!mOutQueue.empty());

    if (!mOutQueue.empty()) {
        scheduleDrainOutQueue();
    }
}

void RTPSocketHandler::onDTLSReceive(const uint8_t *data, size_t size) {
    if (mDTLS) {
        mDTLS->inject(data, size);
    }
}

void RTPSocketHandler::notifyDTLSConnected() {
    LOG(INFO) << "TDLS says that it's now connected.";

    mDTLSConnected = true;

    if (mTrackMask & TRACK_VIDEO) {
        mServerState->getVideoPacketizer()->addSender(mRTPSender);
    }

    if (mTrackMask & TRACK_AUDIO) {
        mServerState->getAudioPacketizer()->addSender(mRTPSender);
    }

    if (mTrackMask & TRACK_DATA) {
        mSCTPHandler = std::make_shared<SCTPHandler>(mRunLoop, mDTLS);
        auto server_state = mServerState;
        mSCTPHandler->onDataChannel(
            "input-channel",
            [server_state](std::shared_ptr<DataChannelStream> data_channel) {
              data_channel->OnMessage(
                  [server_state](const uint8_t *data, size_t size) {
                    ProcessInputEvent(server_state, data, size);
                  });
            });
        mSCTPHandler->run();
    }

    mRTPSender->run();
}

int RTPSocketHandler::onSRTPReceive(uint8_t *data, size_t size) {
    if (size < 2) {
        return -EINVAL;
    }

    auto version = data[0] >> 6;
    if (version != 2) {
        return -EINVAL;
    }

    auto outSize = mDTLS->unprotect(data, size, false /* isRTP */);

    auto err = mRTPSender->injectRTCP(data, outSize);
    if (err) {
        LOG(WARNING) << "RTPSender::injectRTCP returned " << err;
    }

    return err;
}

void RTPSocketHandler::queueRTCPDatagram(const void *data, size_t size) {
    if (!mDTLSConnected) {
        return;
    }

    std::vector<uint8_t> copy(size + SRTP_MAX_TRAILER_LEN);
    memcpy(copy.data(), data, size);

    auto outSize = mDTLS->protect(copy.data(), size, false /* isRTP */);
    CHECK_LE(outSize, copy.size());

    queueDatagram(mSession->remoteAddress(), copy.data(), outSize);
}

void RTPSocketHandler::queueRTPDatagram(const void *data, size_t size) {
    if (!mDTLSConnected) {
        return;
    }

    std::vector<uint8_t> copy(size + SRTP_MAX_TRAILER_LEN);
    memcpy(copy.data(), data, size);

    auto outSize = mDTLS->protect(copy.data(), size, true /* isRTP */);
    CHECK_LE(outSize, copy.size());

    queueDatagram(mSession->remoteAddress(), copy.data(), outSize);
}
