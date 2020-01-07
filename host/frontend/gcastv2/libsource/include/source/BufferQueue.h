#pragma once

#include <utils/Errors.h>

#include <deque>
#include <mutex>

namespace android {

struct BufferQueue {
    BufferQueue(size_t count, size_t size);
    ~BufferQueue();

    status_t initCheck() const;

    size_t bufferSize() const;

    BufferQueue(const BufferQueue &) = delete;
    BufferQueue &operator=(const BufferQueue &) = delete;

    void *acquire(int64_t timeoutUs = -1ll);
    void queue(void *buffer);

    void *dequeueBegin(size_t *size);
    void dequeueEnd(size_t size);

private:
    struct Buffer {
        void *mData;
        size_t mOffset;
    };

    status_t mInitCheck;
    size_t mBufferSize;

    std::mutex mLock;
    std::condition_variable mCondition;

    std::deque<void *> mEmptyBuffers;
    std::deque<Buffer> mFullBuffers;
};

}  // namespace android

