#include <webrtc/H264Packetizer.h>

#include "Utils.h"

#include <webrtc/RTPSocketHandler.h>

#include <https/SafeCallbackable.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/avc_utils.h>
#include <media/stagefright/Utils.h>

using namespace android;

H264Packetizer::H264Packetizer(
        std::shared_ptr<RunLoop> runLoop,
        std::shared_ptr<FrameBufferSource> frameBufferSource)
    : mRunLoop(runLoop),
      mFrameBufferSource(frameBufferSource),
      mNumSamplesRead(0),
      mStartTimeMedia(0) {
}

void H264Packetizer::run() {
    auto weak_this = std::weak_ptr<H264Packetizer>(shared_from_this());

    mFrameBufferSource->setCallback(
            [weak_this](const sp<ABuffer> &accessUnit) {
                auto me = weak_this.lock();
                if (me) {
                    me->mRunLoop->post(
                            makeSafeCallback(
                                me.get(), &H264Packetizer::onFrame, accessUnit));
                }
            });

    mFrameBufferSource->start();
}

void H264Packetizer::onFrame(const sp<ABuffer> &accessUnit) {
    int64_t timeUs;
    CHECK(accessUnit->meta()->findInt64("timeUs", &timeUs));

    auto now = std::chrono::steady_clock::now();

    if (mNumSamplesRead == 0) {
        mStartTimeMedia = timeUs;
        mStartTimeReal = now;
    }

    ++mNumSamplesRead;

    LOG(VERBOSE)
        << "got accessUnit of size "
        << accessUnit->size()
        << " at time "
        << timeUs;

    packetize(accessUnit, timeUs);
}

void H264Packetizer::packetize(const sp<ABuffer> &accessUnit, int64_t timeUs) {
    static constexpr uint8_t PT = 96;
    static constexpr uint32_t SSRC = 0xdeadbeef;
    static constexpr uint8_t STAP_A = 24;
    static constexpr uint8_t FU_A = 28;

    // XXX Retransmission packets add 2 bytes (for the original seqNum), should
    // probably reserve that amount in the original packets so we don't exceed
    // the MTU on retransmission.
    static const size_t kMaxSRTPPayloadSize =
        RTPSocketHandler::kMaxUDPPayloadSize - SRTP_MAX_TRAILER_LEN;

    const uint8_t *data = accessUnit->data();
    size_t size = accessUnit->size();

    uint32_t rtpTime = ((timeUs - mStartTimeMedia) * 9) / 100;

    std::vector<std::pair<size_t, size_t>> nalInfos;

    const uint8_t *nalStart;
    size_t nalSize;
    while (getNextNALUnit(&data, &size, &nalStart, &nalSize, true) == OK) {
        nalInfos.push_back(
                std::make_pair(nalStart - accessUnit->data(), nalSize));
    }

    size_t i = 0;
    while (i < nalInfos.size()) {
        size_t totalSize = 12 + 1;

        uint8_t F = 0;
        uint8_t NRI = 0;

        size_t j = i;
        while (j < nalInfos.size()) {
            auto [nalOffset, nalSize] = nalInfos[j];

            size_t fragASize = 2 + nalSize;
            if (totalSize + fragASize > kMaxSRTPPayloadSize) {
                break;
            }

            uint8_t header = accessUnit->data()[nalOffset];
            F |= (header & 0x80);

            if ((header & 0x60) > NRI) {
                NRI = header & 0x60;
            }

            totalSize += fragASize;

            ++j;
        }

        if (j == i) {
            // Not even a single NALU fits in a STAP-A packet, but may fit
            // inside a single-NALU packet...

            auto [nalOffset, nalSize] = nalInfos[i];
            if (12 + nalSize <= kMaxSRTPPayloadSize) {
                j = i + 1;
            }
        }

        if (j == i) {
            // Not even a single NALU fits, need an FU-A.

            auto [nalOffset, nalSize] = nalInfos[i];

            uint8_t nalHeader = accessUnit->data()[nalOffset];

            size_t offset = 1;
            while (offset < nalSize) {
                size_t copy = std::min(
                        kMaxSRTPPayloadSize - 12 - 2, nalSize - offset);

                bool last = (offset + copy == nalSize);

                std::vector<uint8_t> packet(12 + 2 + copy);

                uint8_t *data = packet.data();
                data[0] = 0x80;

                data[1] = PT;
                if (last && i + 1 == nalInfos.size()) {
                    data[1] |= 0x80;  // (M)ark
                }

                SET_U16(&data[2], 0);  // seqNum
                SET_U32(&data[4], rtpTime);
                SET_U32(&data[8], SSRC);

                data[12] = (nalHeader & 0xe0) | FU_A;

                data[13] = (nalHeader & 0x1f);

                if (offset == 1) {
                    CHECK_LT(offset + copy, nalSize);
                    data[13] |= 0x80;  // (S)tart
                } else if (last) {
                    CHECK_GT(offset, 1u);
                    data[13] |= 0x40;  // (E)nd
                }

                memcpy(&data[14], accessUnit->data() + nalOffset + offset, copy);

                offset += copy;

                LOG(VERBOSE)
                    << "Sending FU-A w/ indicator "
                    << StringPrintf("0x%02x", data[12])
                    << ", header "
                    << StringPrintf("0x%02x", data[13]);

                queueRTPDatagram(&packet);
            }

            ++i;
            continue;
        }

        if (j == i + 1) {
            // Only a single NALU fits.

            auto [nalOffset, nalSize] = nalInfos[i];

            std::vector<uint8_t> packet(12 + nalSize);

            uint8_t *data = packet.data();
            data[0] = 0x80;

            data[1] = PT;
            if (i + 1 == nalInfos.size()) {
                data[1] |= 0x80;  // (M)arker
            }

            SET_U16(&data[2], 0);  // seqNum
            SET_U32(&data[4], rtpTime);
            SET_U32(&data[8], SSRC);

            memcpy(data + 12, accessUnit->data() + nalOffset, nalSize);

            LOG(VERBOSE) << "Sending single NALU of size " << nalSize;

            queueRTPDatagram(&packet);

            ++i;
            continue;
        }

        // STAP-A

        std::vector<uint8_t> packet(totalSize);

        uint8_t *data = packet.data();
        data[0] = 0x80;

        data[1] = PT;
        if (j == nalInfos.size()) {
            data[1] |= 0x80;  // (M)arker
        }

        SET_U16(&data[2], 0);  // seqNum
        SET_U32(&data[4], rtpTime);
        SET_U32(&data[8], SSRC);

        data[12] = F | NRI | STAP_A;

        size_t offset = 13;
        while (i < j) {
            auto [nalOffset, nalSize] = nalInfos[i];

            SET_U16(&data[offset], nalSize);
            memcpy(&data[offset + 2], accessUnit->data() + nalOffset, nalSize);

            offset += 2 + nalSize;

            ++i;
        }

        CHECK_EQ(offset, totalSize);

        LOG(VERBOSE) << "Sending STAP-A of size " << totalSize;

        queueRTPDatagram(&packet);
    }
}

uint32_t H264Packetizer::rtpNow() const {
    if (mNumSamplesRead == 0) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto timeSinceStart = now - mStartTimeReal;

    auto us_since_start =
        std::chrono::duration_cast<std::chrono::microseconds>(
                timeSinceStart).count();

    return (us_since_start * 9) / 100;
}

android::status_t H264Packetizer::requestIDRFrame() {
    return mFrameBufferSource->requestIDRFrame();
}

