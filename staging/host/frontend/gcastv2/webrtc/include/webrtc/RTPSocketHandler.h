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

#include <https/PlainSocket.h>
#include <https/RunLoop.h>
#include <webrtc/DTLS.h>
#include <webrtc/RTPSender.h>
#include <webrtc/RTPSession.h>
#include <webrtc/SCTPHandler.h>
#include <webrtc/ServerState.h>
#include <webrtc/STUNMessage.h>

#include <memory>
#include <string_view>
#include <vector>

struct MyWebSocketHandler;

struct RTPSocketHandler
    : public std::enable_shared_from_this<RTPSocketHandler> {

    static constexpr size_t kMaxUDPPayloadSize = 1536;

    static constexpr uint32_t TRACK_VIDEO = 1;
    static constexpr uint32_t TRACK_AUDIO = 2;
    static constexpr uint32_t TRACK_DATA  = 4;

    enum class TransportType {
        UDP,
        TCP,
    };

    explicit RTPSocketHandler(
            std::shared_ptr<RunLoop> runLoop,
            std::shared_ptr<ServerState> serverState,
            TransportType type,
            int domain,
            uint32_t trackMask,
            std::shared_ptr<RTPSession> session);

    uint16_t getLocalPort() const;
    std::string getLocalUFrag() const;
    std::string getLocalIPString() const;

    void run();

    void queueDatagram(
            const sockaddr_storage &addr, const void *data, size_t size);

    void queueRTCPDatagram(const void *data, size_t size);
    void queueRTPDatagram(const void *data, size_t size);

    void notifyDTLSConnected();

private:
    struct Datagram {
        explicit Datagram(
                const sockaddr_storage &addr, const void *data, size_t size);

        const void *data() const;
        size_t size() const;

        const sockaddr_storage &remoteAddress() const;

    private:
        std::vector<uint8_t> mData;
        sockaddr_storage mAddr;
    };

    std::shared_ptr<RunLoop> mRunLoop;
    std::shared_ptr<ServerState> mServerState;
    TransportType mTransportType;
    uint16_t mLocalPort;
    uint32_t mTrackMask;
    std::shared_ptr<RTPSession> mSession;

    std::shared_ptr<BufferedSocket> mSocket;
    std::shared_ptr<DTLS> mDTLS;
    std::shared_ptr<SCTPHandler> mSCTPHandler;

    std::deque<std::shared_ptr<Datagram>> mOutQueue;
    bool mSendPending;
    bool mDTLSConnected;

    std::shared_ptr<RTPSender> mRTPSender;

    // for TransportType TCP:
    std::shared_ptr<PlainSocket> mServerSocket;
    sockaddr_storage mClientAddr;
    socklen_t mClientAddrLen;

    std::vector<uint8_t> mInBuffer;
    size_t mInBufferLength;

    std::shared_ptr<std::vector<uint8_t>> mTcpOutBuffer;
    std::deque<std::shared_ptr<std::vector<uint8_t>>> mTcpOutBufferQueue;

    void onReceive();
    void onDTLSReceive(const uint8_t *data, size_t size);

    void pingRemote(std::shared_ptr<RTPSession> session);

    bool matchesSession(const STUNMessage &msg) const;

    void scheduleDrainOutQueue();
    void drainOutQueue();

    int onSRTPReceive(uint8_t *data, size_t size);

    void onTCPConnect();
    void onTCPReceive();

    void onPacketReceived(
            const sockaddr_storage &addr,
            socklen_t addrLen,
            uint8_t *data,
            size_t size);

    void queueTCPOutputPacket(const uint8_t *data, size_t size);
    void sendTCPOutputData();
};
