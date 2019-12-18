#include <source/BufferQueue.h>

#include <media/stagefright/foundation/ALooper.h>

namespace android {

BufferQueue::BufferQueue(size_t count, size_t size)
    : mInitCheck(NO_INIT),
      mBufferSize(size) {
    for (size_t i = 0; i < count; ++i) {
        void *buffer = malloc(size);
        if (buffer == nullptr) {
            mInitCheck = -ENOMEM;
            return;
        }

        mEmptyBuffers.push_back(buffer);
    }

    mInitCheck = OK;
}

BufferQueue::~BufferQueue() {
    for (auto buffer : mEmptyBuffers) {
        free(buffer);
    }
}

status_t BufferQueue::initCheck() const {
    return mInitCheck;
}

size_t BufferQueue::bufferSize() const {
    return mBufferSize;
}

void *BufferQueue::acquire(int64_t timeoutUs) {
    int64_t waitUntilUs =
        (timeoutUs < 0ll) ? -1ll : ALooper::GetNowUs() + timeoutUs;

    std::unique_lock<std::mutex> autoLock(mLock);
    while (mEmptyBuffers.empty()) {
        if (waitUntilUs < 0ll) {
            mCondition.wait(autoLock);
        } else {
            int64_t nowUs = ALooper::GetNowUs();

            if (nowUs >= waitUntilUs) {
                break;
            }

            auto result = mCondition.wait_for(
                    autoLock, std::chrono::microseconds(waitUntilUs - nowUs));

            if (result == std::cv_status::timeout) {
                break;
            }
        }
    }

    if (mEmptyBuffers.empty()) {
        return nullptr;
    }

    auto result = mEmptyBuffers.front();
    mEmptyBuffers.pop_front();

    return result;
}

void BufferQueue::queue(void *data) {
    std::lock_guard<std::mutex> autoLock(mLock);
    bool wasEmpty = mFullBuffers.empty();
    mFullBuffers.push_back(Buffer { data, 0 /* offset */ });
    if (wasEmpty) {
        mCondition.notify_all();
    }
}

void *BufferQueue::dequeueBegin(size_t *size) {
    std::lock_guard<std::mutex> autoLock(mLock);
    if (mFullBuffers.empty()) {
        return nullptr;
    }

    Buffer &result = mFullBuffers.front();
    *size = mBufferSize - result.mOffset;

    return static_cast<uint8_t *>(result.mData) + result.mOffset;
}

void BufferQueue::dequeueEnd(size_t size) {
    std::lock_guard<std::mutex> autoLock(mLock);
    CHECK(!mFullBuffers.empty());
    Buffer &result = mFullBuffers.front();
    CHECK_LE(size, mBufferSize - result.mOffset);
    result.mOffset = mBufferSize - size;
    if (size == 0) {
        bool wasEmpty = mEmptyBuffers.empty();
        mEmptyBuffers.push_back(result.mData);

        if (wasEmpty) {
            mCondition.notify_all();
        }

        mFullBuffers.pop_front();
    }
}

}  // namespace android

