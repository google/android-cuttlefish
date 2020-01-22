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

#include <source/StreamingSource.h>

#include <functional>
#include <memory>
#include <thread>

namespace android {

struct FrameBufferSource : public StreamingSource {
    enum class Format {
        VP8,
    };

    explicit FrameBufferSource(Format format);

    FrameBufferSource(const FrameBufferSource &) = delete;
    FrameBufferSource &operator=(const FrameBufferSource &) = delete;

    ~FrameBufferSource() override;

    int32_t initCheck() const override;

    int32_t start() override;
    int32_t stop() override;

    int32_t pause() override;
    int32_t resume() override;

    bool paused() const override;

    int32_t requestIDRFrame() override;
    void notifyNewStreamConsumer() override;
    void notifyStreamConsumerDisconnected() override;

    void setScreenParams(const int32_t screenParams[4]);
    void injectFrame(const void *data, size_t size);

private:
    enum State {
        STOPPING,
        STOPPED,
        RUNNING,
        PAUSED
    };

    struct Encoder;
    struct VPXEncoder;

    int32_t mInitCheck;
    State mState;
    Format mFormat;
    std::unique_ptr<Encoder> mEncoder;

    std::mutex mLock;

    int32_t mScreenWidth, mScreenHeight, mScreenDpi, mScreenRate, mNumConsumers;

    std::function<void(const std::shared_ptr<SBuffer> &)> mOnFrameFn;
};

}  // namespace android


