/*
 * Copyright 2018, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <android-base/logging.h>

#include <cinttypes>
#include <memory>
#include <vector>

namespace android {

class SBuffer {
public:
    SBuffer() = delete;
    SBuffer(const SBuffer&) = delete;
    SBuffer(SBuffer&&) = default;
    explicit SBuffer(std::size_t size)
        : buffer_(size, 0), time_us_(0) {}
    
    SBuffer& operator=(const SBuffer&) = delete;
    SBuffer& operator=(SBuffer&&) = default;

    std::size_t capacity() const {
        return buffer_.capacity();
    }

    std::size_t size() const {
        return buffer_.size();
    }

    void resize(std::size_t size) {
        buffer_.resize(size);
    }

    uint8_t* data() {
        return buffer_.data();
    }
    
    const uint8_t* data() const {
        return buffer_.data();
    }

    int64_t time_us() const {
        return time_us_;
    }

    void time_us(int64_t time_us) {
        time_us_ = time_us;
    }
private:
    std::vector<uint8_t> buffer_;
    int64_t time_us_;

};

struct StreamingSource {
    explicit StreamingSource();

    StreamingSource(const StreamingSource &) = delete;
    StreamingSource &operator=(const StreamingSource &) = delete;

    virtual ~StreamingSource() = default;

    virtual int32_t initCheck() const = 0;

    void setCallback(std::function<void(const std::shared_ptr<SBuffer> &)> cb);

    virtual int32_t start() = 0;
    virtual int32_t stop() = 0;

    virtual bool canPause() { return false; }
    virtual int32_t pause() { return -EINVAL; }
    virtual int32_t resume() { return -EINVAL; }

    virtual bool paused() const { return false; }

    virtual int32_t requestIDRFrame() = 0;
    virtual void notifyNewStreamConsumer() = 0;
    virtual void notifyStreamConsumerDisconnected() = 0;

protected:
    void onAccessUnit(const std::shared_ptr<SBuffer> &accessUnit);

private:
    std::function<void(const std::shared_ptr<SBuffer> &)> mCallbackFn;
};

}  // namespace android

