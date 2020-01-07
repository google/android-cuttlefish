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

#include <stdint.h>

#include <memory>
#include <vector>

struct RTPSender;

struct Packetizer {
    explicit Packetizer() = default;
    virtual ~Packetizer() = default;

    virtual void run() = 0;
    virtual uint32_t rtpNow() const = 0;
    virtual int32_t requestIDRFrame() = 0;

    void queueRTPDatagram(std::vector<uint8_t> *packet);

    void addSender(std::shared_ptr<RTPSender> sender);

private:
    std::vector<std::weak_ptr<RTPSender>> mSenders;
};
