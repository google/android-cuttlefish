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

#include "Packetizer.h"

#include <https/RunLoop.h>

#include <source/HostToGuestComms.h>

#include <source/StreamingSink.h>
#include <source/StreamingSource.h>

#include <memory>
#include <mutex>
#include <set>

#include <host/libs/screen_connector/screen_connector.h>

struct ServerState {
    using StreamingSink = android::StreamingSink;

    enum class VideoFormat {
        VP8,
    };
    explicit ServerState(
            std::shared_ptr<RunLoop> runLoop,
            VideoFormat videoFormat);

    std::shared_ptr<Packetizer> getVideoPacketizer();
    std::shared_ptr<Packetizer> getAudioPacketizer();
    std::shared_ptr<StreamingSink> getTouchSink();

    VideoFormat videoFormat() const { return mVideoFormat; }

    size_t acquireHandlerId();
    void releaseHandlerId(size_t id);

    uint16_t acquirePort();
    void releasePort(uint16_t port);

private:
    using StreamingSource = android::StreamingSource;

    std::shared_ptr<RunLoop> mRunLoop;

    VideoFormat mVideoFormat;

    std::weak_ptr<Packetizer> mVideoPacketizer;
    std::weak_ptr<Packetizer> mAudioPacketizer;

    std::shared_ptr<StreamingSource> mFrameBufferSource;

    std::shared_ptr<StreamingSource> mAudioSource;

    std::shared_ptr<HostToGuestComms> mHostToGuestComms;
    std::shared_ptr<HostToGuestComms> mAudioComms;
    std::shared_ptr<cvd::ScreenConnector> mScreenConnector;
    std::shared_ptr<std::thread> mScreenConnectorMonitor;

    std::shared_ptr<StreamingSink> mTouchSink;

    std::set<size_t> mAllocatedHandlerIds;

    std::mutex mPortLock;
    std::set<uint16_t> mAvailablePorts;

    void changeResolution(int32_t width, int32_t height, int32_t densityDpi);
    void MonitorScreenConnector();
};
