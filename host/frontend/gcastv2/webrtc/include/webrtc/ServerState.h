#pragma once

#include "Packetizer.h"

#include <https/RunLoop.h>

#ifdef TARGET_ANDROID
#include <source/HostToGuestComms.h>
#elif defined(TARGET_ANDROID_DEVICE)
#include <media/stagefright/foundation/ALooper.h>
#include <platform/MyContext.h>
#else
#error "Either TARGET_ANDROID or TARGET_ANDROID_DEVICE must be defined."
#endif

#include <source/StreamingSink.h>
#include <source/StreamingSource.h>

#include <memory>
#include <mutex>
#include <set>

struct ServerState {
    using StreamingSink = android::StreamingSink;

#ifdef TARGET_ANDROID_DEVICE
    template<class T> using sp = android::sp<T>;
    using MyContext = android::MyContext;
#endif

    enum class VideoFormat {
        H264,
        VP8,
    };
    explicit ServerState(
#ifdef TARGET_ANDROID_DEVICE
            const sp<MyContext> &context,
#endif
            std::shared_ptr<RunLoop> runLoop,
            VideoFormat videoFormat);

    std::shared_ptr<Packetizer> getVideoPacketizer();
    std::shared_ptr<Packetizer> getAudioPacketizer();
    std::shared_ptr<StreamingSink> getTouchSink();

    VideoFormat videoFormat() const { return mVideoFormat; }

    size_t acquireHandlerId();
    void releaseHandlerId(size_t id);

    uint16_t acquirePort();
    void releasePort(uint16_t port);

private:
    using StreamingSource = android::StreamingSource;

#ifdef TARGET_ANDROID_DEVICE
    sp<MyContext> mContext;
#endif

    std::shared_ptr<RunLoop> mRunLoop;

    VideoFormat mVideoFormat;

    std::weak_ptr<Packetizer> mVideoPacketizer;
    std::weak_ptr<Packetizer> mAudioPacketizer;

    std::shared_ptr<StreamingSource> mFrameBufferSource;

    std::shared_ptr<StreamingSource> mAudioSource;

#ifdef TARGET_ANDROID
    std::shared_ptr<HostToGuestComms> mHostToGuestComms;
    std::shared_ptr<HostToGuestComms> mFrameBufferComms;
    std::shared_ptr<HostToGuestComms> mAudioComms;
#else
    using ALooper = android::ALooper;
    sp<ALooper> mLooper, mAudioLooper;
#endif

    std::shared_ptr<StreamingSink> mTouchSink;

    std::set<size_t> mAllocatedHandlerIds;

    std::mutex mPortLock;
    std::set<uint16_t> mAvailablePorts;

#ifdef TARGET_ANDROID
    void changeResolution(int32_t width, int32_t height, int32_t densityDpi);
#endif
};
