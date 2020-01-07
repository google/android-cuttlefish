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

#include <webrtc/DTLS.h>

#include <https/RunLoop.h>

#include <memory>

struct SCTPHandler : public std::enable_shared_from_this<SCTPHandler> {
    explicit SCTPHandler(
            std::shared_ptr<RunLoop> runLoop,
            std::shared_ptr<DTLS> dtls);

    void run();

    int inject(uint8_t *data, size_t size);

private:
    std::shared_ptr<RunLoop> mRunLoop;
    std::shared_ptr<DTLS> mDTLS;

    uint32_t mInitiateTag;
    uint32_t mSendingTSN;
    bool mSentGreeting;

    int processChunk(
            uint16_t srcPort,
            const uint8_t *data,
            size_t size,
            bool firstChunk,
            bool lastChunk);

    static uint32_t crc32c(const uint8_t *data, size_t size);

    void onSendGreeting(uint16_t srcPort, size_t index);
};
