/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <https/RunLoop.h>
#include <source/StreamingSink.h>

#include <functional>
#include <memory>
#include <vector>

namespace android {

struct TouchSink
    : public StreamingSink, public std::enable_shared_from_this<TouchSink> {
    explicit TouchSink(std::shared_ptr<RunLoop> runLoop, int serverFd,
                       bool write_virtio_input);
    ~TouchSink() override;

    void start();

    void onAccessUnit(const std::shared_ptr<InputEvent> &accessUnit) override;

private:
    std::shared_ptr<RunLoop> mRunLoop;
    int mServerFd;

    int mClientFd;

    std::mutex mLock;
    std::vector<uint8_t> mOutBuffer;
    bool mSendPending;

    std::function<void(int32_t, int32_t, bool)> send_event_;
    std::function<void(int32_t, int32_t, int32_t, bool, int32_t)> send_mt_event_;

    void onServerConnection();
    void onSocketSend();

    void sendRawEvents(const void* evt_buffer, size_t length);
};

}  // namespace android


