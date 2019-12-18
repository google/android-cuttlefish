#pragma once

#include <utils/Errors.h>

#include <AudioToolbox/AudioToolbox.h>

#include <deque>
#include <mutex>

namespace android {

struct AudioQueueBufferManager {
    AudioQueueBufferManager(AudioQueueRef queue, size_t count, size_t size);
    ~AudioQueueBufferManager();

    status_t initCheck() const;

    size_t bufferSize() const;

    AudioQueueBufferManager(const AudioQueueBufferManager &) = delete;
    AudioQueueBufferManager &operator=(const AudioQueueBufferManager &) = delete;

    AudioQueueBufferRef acquire(int64_t timeoutUs = -1ll);
    void release(AudioQueueBufferRef buffer);

private:
    status_t mInitCheck;
    AudioQueueRef mQueue;
    size_t mBufferSize;

    std::mutex mLock;
    std::condition_variable mCondition;

    std::deque<AudioQueueBufferRef> mBuffers;
};

}  // namespace android

