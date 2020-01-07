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

#include <utils/Errors.h>

#include <memory>

#include <media/stagefright/foundation/AMessage.h>

namespace android {

struct StreamingSource {
    explicit StreamingSource();

    StreamingSource(const StreamingSource &) = delete;
    StreamingSource &operator=(const StreamingSource &) = delete;

    virtual ~StreamingSource() = default;

    virtual status_t initCheck() const = 0;

    void setNotify(const std::shared_ptr<AMessage> &notify);
    void setCallback(std::function<void(const std::shared_ptr<ABuffer> &)> cb);

    virtual void setParameters(const std::shared_ptr<AMessage> &params) {
        (void)params;
    }

    virtual std::shared_ptr<AMessage> getFormat() const = 0;

    virtual status_t start() = 0;
    virtual status_t stop() = 0;

    virtual bool canPause() { return false; }
    virtual status_t pause() { return -EINVAL; }
    virtual status_t resume() { return -EINVAL; }

    virtual bool paused() const { return false; }

    virtual status_t requestIDRFrame() = 0;

protected:
    void onAccessUnit(const std::shared_ptr<ABuffer> &accessUnit);

private:
    std::shared_ptr<AMessage> mNotify;
    std::function<void(const std::shared_ptr<ABuffer> &)> mCallbackFn;
};

}  // namespace android

