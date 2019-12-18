#pragma once

#include "Packetizer.h"

#include <https/RunLoop.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/NuMediaExtractor.h>
#include <source/StreamingSource.h>

#include <memory>

struct VP8Packetizer
    : public Packetizer, public std::enable_shared_from_this<VP8Packetizer> {

    using StreamingSource = android::StreamingSource;

    explicit VP8Packetizer(
            std::shared_ptr<RunLoop> runLoop,
            std::shared_ptr<StreamingSource> frameBufferSource);

    ~VP8Packetizer() override;

    void run() override;
    uint32_t rtpNow() const override;
    android::status_t requestIDRFrame() override;

private:
    template<class T> using sp = android::sp<T>;
    using ABuffer = android::ABuffer;

    std::shared_ptr<RunLoop> mRunLoop;

    std::shared_ptr<StreamingSource> mFrameBufferSource;

    size_t mNumSamplesRead;

    std::chrono::time_point<std::chrono::steady_clock> mStartTimeReal;
    int64_t mStartTimeMedia;

    void onFrame(const sp<ABuffer> &accessUnit);

    void packetize(const sp<ABuffer> &accessUnit, int64_t timeUs);
};

