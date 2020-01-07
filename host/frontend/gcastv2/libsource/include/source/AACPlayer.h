#pragma once

#include "AudioQueueBufferManager.h"
#include "BufferQueue.h"

#include <utils/Errors.h>

#include <sys/types.h>

#include <AudioToolbox/AudioToolbox.h>

#include <memory>

namespace android {

#define USE_AUDIO_UNIT          1

struct AACPlayer {
    explicit AACPlayer();
    ~AACPlayer();

    AACPlayer(const AACPlayer &) = delete;
    AACPlayer &operator=(const AACPlayer &) = delete;

    status_t feedADTSFrame(const void *frame, size_t size);

    int32_t sampleRateHz() const;

private:
    AudioStreamBasicDescription mInFormat, mOutFormat;
    AudioConverterRef mConverter;

#if USE_AUDIO_UNIT
    AUGraph mGraph;
    AUNode mOutputNode;
    std::unique_ptr<BufferQueue> mBufferQueue;
#else
    AudioQueueRef mQueue;
    std::unique_ptr<AudioQueueBufferManager> mBufferManager;
#endif

    int32_t mSampleRateHz;
    int64_t mNumFramesSubmitted;

    status_t init(int32_t sampleRate, size_t channelCount);

#if !USE_AUDIO_UNIT
    static void PlayCallback(
            void *_cookie, AudioQueueRef queue, AudioQueueBufferRef buffer);
#else
    static OSStatus FeedInput(
            void *cookie,
            AudioUnitRenderActionFlags *flags,
            const AudioTimeStamp *timeStamp,
            UInt32 bus,
            UInt32 numFrames,
            AudioBufferList *data);
#endif
};

}  // namespace android
