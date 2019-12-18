#include <source/AudioSink.h>

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

AudioSink::AudioSink()
    : mAACPlayer(new AACPlayer) {
#if LOG_AUDIO
    mFile = fopen("/tmp/audio.aac", "w");
    CHECK(mFile);
#endif
}

void AudioSink::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatAccessUnit:
        {
            sp<ABuffer> accessUnit;
            CHECK(msg->findBuffer("accessUnit", &accessUnit));
            mAACPlayer->feedADTSFrame(accessUnit->data(), accessUnit->size());

#if LOG_AUDIO
            fwrite(accessUnit->data(), 1, accessUnit->size(), mFile);
            fflush(mFile);
#endif
            break;
        }

        default:
            TRESPASS();
    }
}

}  // namespace android

