#pragma once

#include <source/StreamingSink.h>

#include <source/DirectRenderer_iOS.h>

namespace android {

struct VideoSink : public StreamingSink {
    VideoSink();

    void onMessageReceived(const sp<AMessage> &msg) override;

private:
    std::unique_ptr<DirectRenderer_iOS> mRenderer;
    bool mFirstAccessUnit;
};

}  // namespace android

