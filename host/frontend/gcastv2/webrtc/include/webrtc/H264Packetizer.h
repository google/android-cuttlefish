#pragma once

#include "Packetizer.h"

#include <https/RunLoop.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/NuMediaExtractor.h>

#include <memory>

#include <source/StreamingSource.h>

struct H264Packetizer
    : public Packetizer, public std::enable_shared_from_this<H264Packetizer> {
    using StreamingSource = android::StreamingSource;

    explicit H264Packetizer(
            std::shared_ptr<RunLoop> runLoop,
            std::shared_ptr<StreamingSource> frameBufferSource);

    void run() override;
    uint32_t rtpNow() const override;
    android::status_t requestIDRFrame() override;

private:
    using ABuffer = android::ABuffer;
    template<class T> using sp = android::sp<T>;

    std::shared_ptr<RunLoop> mRunLoop;

    std::shared_ptr<StreamingSource> mFrameBufferSource;

    size_t mNumSamplesRead;

    std::chrono::time_point<std::chrono::steady_clock> mStartTimeReal;
    int64_t mStartTimeMedia;

    void onFrame(const sp<ABuffer> &accessUnit);

    void packetize(const sp<ABuffer> &accessUnit, int64_t timeUs);
};

