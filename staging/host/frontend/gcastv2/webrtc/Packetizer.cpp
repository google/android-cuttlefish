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

#include <https/SafeCallbackable.h>
#include <webrtc/RTPSender.h>

Packetizer::Packetizer(std::shared_ptr<RunLoop> runLoop,
                       std::shared_ptr<StreamingSource> source)
    : mNumSamplesRead(0),
      mStartTimeMedia(0),
      mRunLoop(runLoop),
      mStreamingSource(source) {}

Packetizer::~Packetizer() {
    if (mStreamingSource) {
        mStreamingSource->stop();
    }
}

void Packetizer::queueRTPDatagram(std::vector<uint8_t> *packet) {
    auto it = mSenders.begin();
    while (it != mSenders.end()) {
        auto sender = it->lock();
        if (!sender) {
            it = mSenders.erase(it);
            mStreamingSource->notifyStreamConsumerDisconnected();
            continue;
        }

        sender->queueRTPDatagram(packet);
        ++it;
    }
}

void Packetizer::addSender(std::shared_ptr<RTPSender> sender) {
    mSenders.push_back(sender);
    auto weak_source = std::weak_ptr<StreamingSource>(mStreamingSource);
    mRunLoop->post([weak_source](){
        auto source = weak_source.lock();
        if (!source) return;
        source->notifyNewStreamConsumer();
    });
}

int32_t Packetizer::requestIDRFrame() {
    return mStreamingSource->requestIDRFrame();
}

void Packetizer::run() {
    auto weak_this = weak_from_this();

    mStreamingSource->setCallback(
            [weak_this](const std::shared_ptr<android::SBuffer> &accessUnit) {
                auto me = weak_this.lock();
                if (me) {
                    me->mRunLoop->post(
                            makeSafeCallback(
                                me.get(), &Packetizer::onFrame, accessUnit));
                }
            });

    mStreamingSource->start();
}

void Packetizer::onFrame(const std::shared_ptr<android::SBuffer>& accessUnit) {
    if (!accessUnit) {
        LOG(WARNING) << "Received invalid buffer in " << __FUNCTION__;
        return;
    }
    int64_t timeUs = accessUnit->time_us();

    auto now = std::chrono::steady_clock::now();

    if (mNumSamplesRead == 0) {
        mStartTimeMedia = timeUs;
        mStartTimeReal = now;
    }

    ++mNumSamplesRead;

    LOG(VERBOSE)
        << "got accessUnit of size "
        << accessUnit->size()
        << " at time "
        << timeUs;

    packetize(accessUnit, timeUs);
}

uint32_t Packetizer::timeSinceStart() const {
    if (mNumSamplesRead == 0) return 0;

    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now -
                                                                 mStartTimeReal)
        .count();
}
