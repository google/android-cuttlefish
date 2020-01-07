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

#include <webrtc/Packetizer.h>

#include <webrtc/RTPSender.h>

void Packetizer::queueRTPDatagram(std::vector<uint8_t> *packet) {
    auto it = mSenders.begin();
    while (it != mSenders.end()) {
        auto sender = it->lock();
        if (!sender) {
            it = mSenders.erase(it);
            continue;
        }

        sender->queueRTPDatagram(packet);
        ++it;
    }
}

void Packetizer::addSender(std::shared_ptr<RTPSender> sender) {
    mSenders.push_back(sender);
}

