#include <webrtc/OpusPacketizer.h>

#include "Utils.h"

#include <webrtc/RTPSocketHandler.h>

#include <https/SafeCallbackable.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/Utils.h>

using namespace android;

OpusPacketizer::OpusPacketizer(
        std::shared_ptr<RunLoop> runLoop,
        std::shared_ptr<StreamingSource> audioSource)
    : mRunLoop(runLoop),
      mAudioSource(audioSource),
      mNumSamplesRead(0),
      mStartTimeMedia(0),
      mFirstInTalkspurt(true) {
}

void OpusPacketizer::run() {
    auto weak_this = std::weak_ptr<OpusPacketizer>(shared_from_this());

    mAudioSource->setCallback(
            [weak_this](const std::shared_ptr<ABuffer> &accessUnit) {
                auto me = weak_this.lock();
                if (me) {
                    me->mRunLoop->post(
                            makeSafeCallback(
                                me.get(), &OpusPacketizer::onFrame, accessUnit));
                }
            });

    mAudioSource->start();
}

void OpusPacketizer::onFrame(const std::shared_ptr<ABuffer> &accessUnit) {
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

void OpusPacketizer::packetize(const std::shared_ptr<ABuffer> &accessUnit, int64_t timeUs) {
    LOG(VERBOSE) << "Received Opus frame of size " << accessUnit->size();

    static constexpr uint8_t PT = 98;
    static constexpr uint32_t SSRC = 0x8badf00d;

    // XXX Retransmission packets add 2 bytes (for the original seqNum), should
    // probably reserve that amount in the original packets so we don't exceed
    // the MTU on retransmission.
    static const size_t kMaxSRTPPayloadSize =
        RTPSocketHandler::kMaxUDPPayloadSize - SRTP_MAX_TRAILER_LEN;

    const uint8_t *audioData = accessUnit->data();
    size_t size = accessUnit->size();

    uint32_t rtpTime = ((timeUs - mStartTimeMedia) * 48) / 1000;

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

uint32_t OpusPacketizer::rtpNow() const {
    if (mNumSamplesRead == 0) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto timeSinceStart = now - mStartTimeReal;

    auto us_since_start =
        std::chrono::duration_cast<std::chrono::microseconds>(
                timeSinceStart).count();

    return (us_since_start * 48) / 1000;
}

android::status_t OpusPacketizer::requestIDRFrame() {
    return mAudioSource->requestIDRFrame();
}

