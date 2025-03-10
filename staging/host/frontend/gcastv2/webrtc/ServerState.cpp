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

DECLARE_int32(touch_fd);
DECLARE_int32(frame_server_fd);
DECLARE_bool(write_virtio_input);

ServerState::ServerState(
        std::shared_ptr<RunLoop> runLoop, VideoFormat videoFormat)
    :
      mRunLoop(runLoop),
      mVideoFormat(videoFormat) {

    // This is the list of ports we currently instruct the firewall to open.
    mAvailablePorts.insert(
            { 15550, 15551, 15552, 15553, 15554, 15555, 15556, 15557 });

    auto config = vsoc::CuttlefishConfig::Get();

    mHostToGuestComms = std::make_shared<HostToGuestComms>(
            mRunLoop,
            false /* isServer */,
            vsoc::GetDefaultPerInstanceVsockCid(),
            HostToGuestComms::kPortMain,
            [](const void *data, size_t size) {
                (void)data;
                LOG(VERBOSE) << "Received " << size << " bytes from guest.";
            });

    mHostToGuestComms->start();

    changeResolution(1440 /* width */, 2880 /* height */, 524 /* dpi */);

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

    mAudioComms = std::make_shared<HostToGuestComms>(
            mRunLoop,
            true /* isServer */,
            VMADDR_CID_HOST,
            HostToGuestComms::kPortAudio,
            [this](const void *data, size_t size) {
                LOG(VERBOSE)
                    << "Received packet of "
                    << size
                    << " bytes of data from audio HAL.";

                static_cast<android::AudioSource *>(
                        mAudioSource.get())->inject(data, size);
            });

    mAudioComms->start();
    
    CHECK_GE(FLAGS_touch_fd, 0);

    auto touchSink =
        std::make_shared<android::TouchSink>(mRunLoop, FLAGS_touch_fd, FLAGS_write_virtio_input);

    touchSink->start();

    mTouchSink = touchSink;
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
    auto packetizer = mVideoPacketizer.lock();
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
    auto packetizer = mAudioPacketizer.lock();
    if (!packetizer) {
        packetizer = std::make_shared<OpusPacketizer>(mRunLoop, mAudioSource);
        packetizer->run();

        mAudioPacketizer = packetizer;
    }

    return packetizer;
}

size_t ServerState::acquireHandlerId() {
    size_t id = 0;
    while (!mAllocatedHandlerIds.insert(id).second) {
        ++id;
    }

    return id;
}

void ServerState::releaseHandlerId(size_t id) {
    CHECK_EQ(mAllocatedHandlerIds.erase(id), 1);
}

uint16_t ServerState::acquirePort() {
    std::lock_guard autoLock(mPortLock);

    if (mAvailablePorts.empty()) {
        return 0;
    }

    uint16_t port = *mAvailablePorts.begin();
    mAvailablePorts.erase(mAvailablePorts.begin());

    return port;
}

void ServerState::releasePort(uint16_t port) {
    CHECK(port);

    std::lock_guard autoLock(mPortLock);
    mAvailablePorts.insert(port);
}

std::shared_ptr<android::StreamingSink> ServerState::getTouchSink() {
    return mTouchSink;
}

void ServerState::changeResolution(
        int32_t width, int32_t height, int32_t densityDpi) {
    LOG(INFO)
        << "Requested dimensions: "
        << width
        << "x"
        << height
        << " @"
        << densityDpi
        << " dpi";

    /* Arguments "width", "height" and "densityDpi" need to be matched to the
     * native screen dimensions specified as "launch_cvd" arguments "x_res"
     * and "y_res".
     */

    auto config = vsoc::CuttlefishConfig::Get();
    const int nativeWidth = config->x_res();
    const int nativeHeight = config->y_res();

    const float ratio = (float)width / (float)height;
    int32_t outWidth = nativeWidth;
    int32_t outHeight = (int32_t)((float)outWidth / ratio);

    if (outHeight > nativeHeight) {
        outHeight = nativeHeight;
        outWidth = (int32_t)((float)outHeight * ratio);
    }

    const int32_t outDensity =
        (int32_t)((float)densityDpi * (float)outWidth / (float)width);

    LOG(INFO)
        << "Scaled dimensions: "
        << outWidth
        << "x"
        << outHeight
        << " @"
        << outDensity
        << " dpi";

    enum class PacketType : uint8_t {
        CHANGE_RESOLUTION = 6,
    };

    static constexpr size_t totalSize =
        sizeof(PacketType) + 3 * sizeof(int32_t);

    std::unique_ptr<uint8_t[]> packet(new uint8_t[totalSize]);
    size_t offset = 0;

    auto append = [ptr = packet.get(), &offset](
            const void *data, size_t len) {
        CHECK_LE(offset + len, totalSize);
        memcpy(ptr + offset, data, len);
        offset += len;
    };

    static constexpr PacketType type = PacketType::CHANGE_RESOLUTION;
    append(&type, sizeof(type));
    append(&outWidth, sizeof(outWidth));
    append(&outHeight, sizeof(outHeight));
    append(&outDensity, sizeof(outDensity));

    CHECK_EQ(offset, totalSize);

    mHostToGuestComms->send(packet.get(), totalSize);
}
