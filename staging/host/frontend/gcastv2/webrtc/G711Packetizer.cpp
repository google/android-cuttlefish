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

#include <webrtc/G711Packetizer.h>

#include "Utils.h"

#include <webrtc/RTPSocketHandler.h>

#include <https/SafeCallbackable.h>

using namespace android;

G711Packetizer::G711Packetizer(
        Mode mode,
        std::shared_ptr<RunLoop> runLoop,
        std::shared_ptr<StreamingSource> audioSource)
    : Packetizer(runLoop, audioSource),
      mMode(mode),
      mFirstInTalkspurt(true) {
}

void G711Packetizer::packetize(const std::shared_ptr<SBuffer> &accessUnit, int64_t timeUs) {
    LOG(VERBOSE) << "Received G711 frame of size " << accessUnit->size();

    const uint8_t PT = (mMode == Mode::ALAW) ? 8 : 0;
    static constexpr uint32_t SSRC = 0x8badf00d;

    // XXX Retransmission packets add 2 bytes (for the original seqNum), should
    // probably reserve that amount in the original packets so we don't exceed
    // the MTU on retransmission.
    static const size_t kMaxSRTPPayloadSize =
        RTPSocketHandler::kMaxUDPPayloadSize - SRTP_MAX_TRAILER_LEN;

    const uint8_t *audioData = accessUnit->data();
    size_t size = accessUnit->size();

    uint32_t rtpTime = ((timeUs - mediaStartTime()) * 8) / 1000;

    CHECK_LE(12 + size, kMaxSRTPPayloadSize);

    std::vector<uint8_t> packet(12 + size);
    uint8_t *data = packet.data();

    packet[0] = 0x80;
    packet[1] = PT;

    if (mFirstInTalkspurt) {
        packet[1] |= 0x80;  // (M)ark
        mFirstInTalkspurt = false;
    }

    SET_U16(&data[2], 0);  // seqNum
    SET_U32(&data[4], rtpTime);
    SET_U32(&data[8], SSRC);

    memcpy(&data[12], audioData, size);

    queueRTPDatagram(&packet);
}

uint32_t G711Packetizer::rtpNow() const {
    return (timeSinceStart() * 8) / 1000;
}
