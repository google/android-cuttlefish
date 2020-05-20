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

#include <webrtc/ServerState.h>

#include <webrtc/OpusPacketizer.h>
#include <webrtc/VP8Packetizer.h>

#include <source/AudioSource.h>
#include <source/TouchSink.h>
#include <source/FrameBufferSource.h>

#include "host/libs/config/cuttlefish_config.h"

#include <gflags/gflags.h>

DECLARE_int32(keyboard_fd);
DECLARE_int32(touch_fd);
DECLARE_int32(frame_server_fd);
DECLARE_bool(write_virtio_input);

ServerState::ServerState(
        std::shared_ptr<RunLoop> runLoop, VideoFormat videoFormat)
    :
      mRunLoop(runLoop),
      mVideoFormat(videoFormat) {

    auto config = vsoc::CuttlefishConfig::Get();

    android::FrameBufferSource::Format fbSourceFormat;
    switch (videoFormat) {
        case VideoFormat::VP8:
            fbSourceFormat = android::FrameBufferSource::Format::VP8;
            break;
        default:
            LOG(FATAL) << "Should not be here.";
    }

    mFrameBufferSource =
        std::make_shared<android::FrameBufferSource>(fbSourceFormat);

    int32_t screenParams[4];
    screenParams[0] = config->x_res();
    screenParams[1] = config->y_res();
    screenParams[2] = config->dpi();
    screenParams[3] = config->refresh_rate_hz();

    static_cast<android::FrameBufferSource *>(
            mFrameBufferSource.get())->setScreenParams(screenParams);

    mScreenConnector = std::shared_ptr<cvd::ScreenConnector>(
        cvd::ScreenConnector::Get(FLAGS_frame_server_fd));
    mScreenConnectorMonitor.reset(
        new std::thread([this]() { MonitorScreenConnector(); }));

    mAudioSource = std::make_shared<android::AudioSource>(
            android::AudioSource::Format::OPUS);

    CHECK_GE(FLAGS_touch_fd, 0);

    auto touchSink = std::make_shared<android::TouchSink>(
        mRunLoop, FLAGS_touch_fd, FLAGS_write_virtio_input);

    touchSink->start();

    mTouchSink = touchSink;

    auto keyboardSink = std::make_shared<android::KeyboardSink>(
        mRunLoop, FLAGS_keyboard_fd, FLAGS_write_virtio_input);

    keyboardSink->start();

    mKeyboardSink = keyboardSink;
}

void ServerState::MonitorScreenConnector() {
    std::uint32_t last_frame = 0;
    while (true) {
      mScreenConnector->OnFrameAfter(last_frame, [this, &last_frame](
                                                     std::uint32_t frame_num,
                                                     std::uint8_t *data) {
        mRunLoop->postAndAwait([this, data]() {
          static_cast<android::FrameBufferSource *>(mFrameBufferSource.get())
              ->injectFrame(data, cvd::ScreenConnector::ScreenSizeInBytes());
        });
        last_frame = frame_num;
      });
    }
}

std::shared_ptr<Packetizer> ServerState::getVideoPacketizer() {
    std::lock_guard<std::mutex> autoLock(mPacketizerLock);
    std::shared_ptr<Packetizer> packetizer = mVideoPacketizer;
    if (!packetizer) {
        switch (mVideoFormat) {
            case VideoFormat::VP8:
            {
                packetizer = std::make_shared<VP8Packetizer>(
                        mRunLoop, mFrameBufferSource);
                break;
            }

            default:
                LOG(FATAL) << "Should not be here.";
        }

        packetizer->run();

        mVideoPacketizer = packetizer;
    }

    return packetizer;
}

std::shared_ptr<Packetizer> ServerState::getAudioPacketizer() {
    std::lock_guard<std::mutex> autoLock(mPacketizerLock);
    std::shared_ptr packetizer = mAudioPacketizer;
    if (!packetizer) {
        packetizer = std::make_shared<OpusPacketizer>(mRunLoop, mAudioSource);
        packetizer->run();

        mAudioPacketizer = packetizer;
    }

    return packetizer;
}

std::shared_ptr<android::TouchSink> ServerState::getTouchSink() {
    return mTouchSink;
}

std::shared_ptr<android::KeyboardSink> ServerState::getKeyboardSink() {
    return mKeyboardSink;
}
