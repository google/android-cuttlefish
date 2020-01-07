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

#include "Packetizer.h"

#include <https/RunLoop.h>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

struct RTPSocketHandler;

struct RTPSender : public std::enable_shared_from_this<RTPSender> {

    explicit RTPSender(
            std::shared_ptr<RunLoop> runLoop,
            RTPSocketHandler *parent,
            std::shared_ptr<Packetizer> videoPacketizer,
            std::shared_ptr<Packetizer> audioPacketizer);

    void addSource(uint32_t ssrc);

    void addRetransInfo(
            uint32_t ssrc, uint8_t PT, uint32_t retransSSRC, uint8_t retransPT);

    int injectRTCP(uint8_t *data, size_t size);
    void queueRTPDatagram(std::vector<uint8_t> *packet);

    void run();

    void requestIDRFrame();

private:
    struct SourceInfo {
        explicit SourceInfo()
            : mNumPacketsSent(0),
              mNumBytesSent(0) {
        }

        size_t mNumPacketsSent;
        size_t mNumBytesSent;

        // (ssrc, PT) by PT.
        std::unordered_map<uint8_t, std::pair<uint32_t, uint8_t>> mRetrans;

        std::deque<std::vector<uint8_t>> mRecentPackets;
    };

    std::shared_ptr<RunLoop> mRunLoop;
    RTPSocketHandler *mParent;

    // Sources by ssrc.
    std::unordered_map<uint32_t, SourceInfo> mSources;

    std::shared_ptr<Packetizer> mVideoPacketizer;
    std::shared_ptr<Packetizer> mAudioPacketizer;

    void appendSR(std::vector<uint8_t> *buffer, uint32_t localSSRC);
    void appendSDES(std::vector<uint8_t> *buffer, uint32_t localSSRC);

    void queueSR(uint32_t localSSRC);
    void sendSR(uint32_t localSSRC);

    void queueDLRR(
            uint32_t localSSRC,
            uint32_t remoteSSRC,
            uint32_t ntpHi,
            uint32_t ntpLo);

    void appendDLRR(
            std::vector<uint8_t> *buffer,
            uint32_t localSSRC,
            uint32_t remoteSSRC,
            uint32_t ntpHi,
            uint32_t ntpLo);

    int processRTCP(const uint8_t *data, size_t size);

    void retransmitPackets(uint32_t localSSRC, uint16_t PID, uint16_t BLP);
};

