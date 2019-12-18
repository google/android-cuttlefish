#include <source/AudioQueueBufferManager.h>

#include <media/stagefright/foundation/ALooper.h>

namespace android {

AudioQueueBufferManager::AudioQueueBufferManager(
        AudioQueueRef queue, size_t count, size_t size)
    : mInitCheck(NO_INIT),
      mQueue(queue),
      mBufferSize(size) {
    for (size_t i = 0; i < count; ++i) {
        AudioQueueBufferRef buffer;
        OSStatus err = AudioQueueAllocateBuffer(mQueue, static_cast<UInt32>(size), &buffer);

        if (err != noErr) {
            mInitCheck = -ENOMEM;
            return;
        }

        mBuffers.push_back(buffer);
    }

    mInitCheck = OK;
}

AudioQueueBufferManager::~AudioQueueBufferManager() {
    for (auto buffer : mBuffers) {
        AudioQueueFreeBuffer(mQueue, buffer);
    }
}

status_t AudioQueueBufferManager::initCheck() const {
    return mInitCheck;
}

size_t AudioQueueBufferManager::bufferSize() const {
    return mBufferSize;
}

AudioQueueBufferRef AudioQueueBufferManager::acquire(int64_t timeoutUs) {
    int64_t waitUntilUs =
        (timeoutUs < 0ll) ? -1ll : ALooper::GetNowUs() + timeoutUs;

    std::unique_lock<std::mutex> autoLock(mLock);
    while (mBuffers.empty()) {
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

    if (mBuffers.empty()) {
        return nullptr;
    }

    auto result = mBuffers.front();
    mBuffers.pop_front();

    return result;
}

void AudioQueueBufferManager::release(AudioQueueBufferRef buffer) {
    std::lock_guard<std::mutex> autoLock(mLock);
    bool wasEmpty = mBuffers.empty();
    mBuffers.push_back(buffer);
    if (wasEmpty) {
        mCondition.notify_all();
    }
}

}  // namespace android

