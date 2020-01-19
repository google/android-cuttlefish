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

#include <https/PlainSocket.h>
#include <https/RunLoop.h>
#include <memory>

#include <arpa/inet.h>

struct STUNClient : public std::enable_shared_from_this<STUNClient> {
    using Callback = std::function<void(int, const std::string &)>;

    explicit STUNClient(
            std::shared_ptr<RunLoop> runLoop,
            const sockaddr_in &addr,
            Callback cb);

    void run();

private:
    static constexpr size_t kMaxUDPPayloadSize = 1536;
    static constexpr size_t kMaxNumRetries = 5;

    static constexpr std::chrono::duration kTimeoutDelay =
        std::chrono::seconds(1);

    std::shared_ptr<RunLoop> mRunLoop;
    sockaddr_in mRemoteAddr;
    Callback mCallback;

    std::shared_ptr<PlainSocket> mSocket;

    RunLoop::Token mTimeoutToken;
    size_t mNumRetriesLeft;

    void onSendRequest();
    void onReceiveResponse();

    void scheduleRequest();
    void onTimeout();
};
