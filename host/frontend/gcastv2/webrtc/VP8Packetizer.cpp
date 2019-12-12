#include <webrtc/VP8Packetizer.h>

#include "Utils.h"

#include <webrtc/RTPSocketHandler.h>

#include <https/SafeCallbackable.h>
#include <https/Support.h>

using namespace android;

VP8Packetizer::VP8Packetizer(
        std::shared_ptr<RunLoop> runLoop,
        std::shared_ptr<StreamingSource> frameBufferSource)
    : mRunLoop(runLoop),
      mFrameBufferSource(frameBufferSource),
      mNumSamplesRead(0),
      mStartTimeMedia(0) {
}

VP8Packetizer::~VP8Packetizer() {
    if (mFrameBufferSource) {
        mFrameBufferSource->stop();
    }
}

void VP8Packetizer::run() {
    auto weak_this = std::weak_ptr<VP8Packetizer>(shared_from_this());

    mFrameBufferSource->setCallback(
            [weak_this](const std::shared_ptr<SBuffer> &accessUnit) {
                auto me = weak_this.lock();
                if (me) {
                    me->mRunLoop->post(
                            makeSafeCallback(
                                me.get(), &VP8Packetizer::onFrame, accessUnit));
                }
            });

    mFrameBufferSource->start();
}

void VP8Packetizer::onFrame(const std::shared_ptr<SBuffer> &accessUnit) {
    int64_t timeUs = accessUnit->time_us();
    CHECK(timeUs);

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

    uint32_t rtpTime = ((timeUs - mStartTimeMedia) * 9) / 100;

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

int32_t VP8Packetizer::requestIDRFrame() {
    return mFrameBufferSource->requestIDRFrame();
}

