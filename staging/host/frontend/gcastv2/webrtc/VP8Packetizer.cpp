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

#include <webrtc/VP8Packetizer.h>

#include "Utils.h"

#include <webrtc/RTPSocketHandler.h>

#include <https/SafeCallbackable.h>
#include <https/Support.h>

using namespace android;

VP8Packetizer::VP8Packetizer(
        std::shared_ptr<RunLoop> runLoop,
        std::shared_ptr<StreamingSource> frameBufferSource)
    : Packetizer(runLoop, frameBufferSource) {
}

void VP8Packetizer::packetize(const std::shared_ptr<SBuffer> &accessUnit, int64_t timeUs) {
    static constexpr uint8_t PT = 96;
    static constexpr uint32_t SSRC = 0xdeadbeef;

    // XXX Retransmission packets add 2 bytes (for the original seqNum), should
    // probably reserve that amount in the original packets so we don't exceed
    // the MTU on retransmission.
    static const size_t kMaxSRTPPayloadSize =
        RTPSocketHandler::kMaxUDPPayloadSize - SRTP_MAX_TRAILER_LEN;

    const uint8_t *src = accessUnit->data();
    size_t srcSize = accessUnit->size();

    uint32_t rtpTime = ((timeUs - mediaStartTime()) * 9) / 100;

    LOG(VERBOSE) << "got accessUnit of size " << srcSize;

    size_t srcOffset = 0;
    while (srcOffset < srcSize) {
        size_t packetSize = 12;  // generic RTP header

        packetSize += 1;  // VP8 Payload Descriptor

        auto copy = std::min(srcSize - srcOffset, kMaxSRTPPayloadSize - packetSize);

        packetSize += copy;

        std::vector<uint8_t> packet(packetSize);
        uint8_t *dst = packet.data();

        dst[0] = 0x80;

        dst[1] = PT;
        if (srcOffset + copy == srcSize) {
            dst[1] |= 0x80;  // (M)ark
        }

        SET_U16(&dst[2], 0);  // seqNum
        SET_U32(&dst[4], rtpTime);
        SET_U32(&dst[8], SSRC);

        size_t dstOffset = 12;

        // VP8 Payload Descriptor
        dst[dstOffset++] = (srcOffset == 0) ? 0x10 : 0x00;  // S

        memcpy(&dst[dstOffset], &src[srcOffset], copy);
        dstOffset += copy;

        CHECK_EQ(dstOffset, packetSize);

        srcOffset += copy;

        queueRTPDatagram(&packet);
    }
}

uint32_t VP8Packetizer::rtpNow() const {
    return (timeSinceStart() * 90) / 1000;
}
