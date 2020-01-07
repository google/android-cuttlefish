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

struct ABuffer;

struct FrameBufferSource : public StreamingSource {
    enum class Format {
        VP8,
    };

    explicit FrameBufferSource(Format format);

    FrameBufferSource(const FrameBufferSource &) = delete;
    FrameBufferSource &operator=(const FrameBufferSource &) = delete;

    ~FrameBufferSource() override;

    status_t initCheck() const override;

    status_t start() override;
    status_t stop() override;

    status_t pause() override;
    status_t resume() override;

    bool paused() const override;

    status_t requestIDRFrame() override;

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

    status_t mInitCheck;
    State mState;
    Format mFormat;
    std::unique_ptr<Encoder> mEncoder;

    std::mutex mLock;

    int32_t mScreenWidth, mScreenHeight, mScreenDpi, mScreenRate;

    std::function<void(const std::shared_ptr<ABuffer> &)> mOnFrameFn;
};

}  // namespace android


