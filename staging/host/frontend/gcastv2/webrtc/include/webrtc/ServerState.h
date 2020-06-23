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

#include <source/KeyboardSink.h>
#include <source/TouchSink.h>
#include <source/StreamingSource.h>

#include <memory>
#include <mutex>
#include <set>

#include <host/libs/screen_connector/screen_connector.h>

struct ServerState {
    using TouchSink = android::TouchSink;
    using KeyboardSink = android::KeyboardSink;

    enum class VideoFormat {
        VP8,
    };
    explicit ServerState(
            std::shared_ptr<RunLoop> runLoop,
            VideoFormat videoFormat);

    std::shared_ptr<Packetizer> getVideoPacketizer();
    std::shared_ptr<Packetizer> getAudioPacketizer();
    std::shared_ptr<TouchSink> getTouchSink();
    std::shared_ptr<KeyboardSink> getKeyboardSink();

    VideoFormat videoFormat() const { return mVideoFormat; }

    std::shared_ptr<RunLoop> run_loop() { return mRunLoop; }
    std::string public_ip() const { return mPublicIp; }
    void SetPublicIp(const std::string& public_ip) { mPublicIp = public_ip; }

   private:
    using StreamingSource = android::StreamingSource;

    std::shared_ptr<RunLoop> mRunLoop;

    VideoFormat mVideoFormat;

    std::mutex mPacketizerLock;

    std::shared_ptr<Packetizer> mVideoPacketizer;
    std::shared_ptr<Packetizer> mAudioPacketizer;

    std::shared_ptr<StreamingSource> mFrameBufferSource;

    std::shared_ptr<StreamingSource> mAudioSource;

    std::shared_ptr<cuttlefish::ScreenConnector> mScreenConnector;
    std::shared_ptr<std::thread> mScreenConnectorMonitor;

    std::shared_ptr<TouchSink> mTouchSink;
    std::shared_ptr<KeyboardSink> mKeyboardSink;

    std::string mPublicIp;

    void MonitorScreenConnector();
};
