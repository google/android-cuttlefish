#pragma once

#include <source/StreamingSink.h>

#include "AACPlayer.h"

#define LOG_AUDIO       0

namespace android {

struct AudioSink : public StreamingSink {
    AudioSink();

    void onMessageReceived(const sp<AMessage> &msg) override;

private:
    std::unique_ptr<AACPlayer> mAACPlayer;

#if LOG_AUDIO
    FILE *mFile;
#endif
};

}  // namespace android

