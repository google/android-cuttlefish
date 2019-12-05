#include <webrtc/G711Packetizer.h>

#include "Utils.h"

#include <webrtc/RTPSocketHandler.h>

#include <https/SafeCallbackable.h>
#include <media/stagefright/Utils.h>

using namespace android;

G711Packetizer::G711Packetizer(
        Mode mode,
        std::shared_ptr<RunLoop> runLoop,
        std::shared_ptr<StreamingSource> audioSource)
    : mMode(mode),
      mRunLoop(runLoop),
      mAudioSource(audioSource),
      mNumSamplesRead(0),
      mStartTimeMedia(0),
      mFirstInTalkspurt(true) {
}

void G711Packetizer::run() {
    auto weak_this = std::weak_ptr<G711Packetizer>(shared_from_this());

    mAudioSource->setCallback(
            [weak_this](const std::shared_ptr<ABuffer> &accessUnit) {
                auto me = weak_this.lock();
                if (me) {
                    me->mRunLoop->post(
                            makeSafeCallback(
                                me.get(), &G711Packetizer::onFrame, accessUnit));
                }
            });

    mAudioSource->start();
}

void G711Packetizer::onFrame(const std::shared_ptr<ABuffer> &accessUnit) {
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

void G711Packetizer::packetize(const std::shared_ptr<ABuffer> &accessUnit, int64_t timeUs) {
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

    uint32_t rtpTime = ((timeUs - mStartTimeMedia) * 8) / 1000;

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
    if (mNumSamplesRead == 0) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto timeSinceStart = now - mStartTimeReal;

    auto us_since_start =
        std::chrono::duration_cast<std::chrono::microseconds>(
                timeSinceStart).count();

    return (us_since_start * 8) / 1000;
}

android::status_t G711Packetizer::requestIDRFrame() {
    return mAudioSource->requestIDRFrame();
}

