/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <assert.h>
#include <fcntl.h>
#include <log/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "guest/commands/v4l2_streamer/v4l2_helpers.h"
#include "guest/commands/v4l2_streamer/vsock_frame_source.h"
#include "guest/commands/v4l2_streamer/yuv2rgb.h"

namespace cuttlefish {

VsockFrameSource::~VsockFrameSource() { Stop(); }

bool VsockFrameSource::IsBlob(const std::vector<char>& blob) {
  static const char kPng[] = "\x89PNG";
  static const char kJpeg[] = "\xff\xd8";
  bool is_png =
      blob.size() > 4 && std::memcmp(blob.data(), kPng, sizeof(kPng)) == 0;
  bool is_jpeg =
      blob.size() > 2 && std::memcmp(blob.data(), kJpeg, sizeof(kJpeg)) == 0;
  return is_png || is_jpeg;
}

bool VsockFrameSource::WriteJsonEventMessage(const std::string& message) {
  Json::Value json_message;
  json_message["event"] = message;
  return connection_ && connection_->WriteMessage(json_message);
}

Result<bool> VsockFrameSource::ReadSettingsFromJson(const Json::Value& json) {
  frame_width_ = json["width"].asInt();
  frame_height_ = json["height"].asInt();
  frame_rate_ = json["frame_rate"].asDouble();

  if (frame_width_ > 0 && frame_height_ > 0 && frame_rate_ > 0) {
    frame_size_ =
        CF_EXPECT(V4l2GetFrameSize(format_, frame_width_, frame_height_),
                  "Error getting framesize");
    ALOGI("%s: readSettingsFromJson received: w/h/fps(%d,%d,%d)", __FUNCTION__,
          frame_width_, frame_height_, frame_rate_);
    return true;
  } else {
    ALOGE("%s: readSettingsFromJson received invalid values: w/h/fps(%d,%d,%d)",
          __FUNCTION__, frame_width_, frame_height_, frame_rate_);
    return false;
  }
}

bool VsockFrameSource::Connect() {
  connection_ = std::make_unique<
      cuttlefish::VsockServerConnection>();  // VsockServerConnection
  if (connection_->Connect(
          7600, VMADDR_CID_ANY,
          std::nullopt /* vhost_user_vsock: because it's guest */)) {
    auto json_settings = connection_->ReadJsonMessage();

    if (ReadSettingsFromJson(json_settings)) {
      std::lock_guard<std::mutex> lock(settings_mutex_);
      ALOGI("%s: VsockFrameSource connected", __FUNCTION__);
      return true;
    } else {
      ALOGE("%s: Could not read settings", __FUNCTION__);
    }
  } else {
    ALOGE("%s: VsockFrameSource connection failed", __FUNCTION__);
  }
  return false;
}

Result<std::unique_ptr<VsockFrameSource>> VsockFrameSource::Start(
    const std::string& v4l2_device_path) {
  auto frame_source = std::unique_ptr<VsockFrameSource>(new VsockFrameSource);

  frame_source->v4l2_device_path_ = v4l2_device_path;

  CF_EXPECT(frame_source->Connect(), "connect failed");

  ALOGI("%s: VsockFrameSource connected", __FUNCTION__);

  frame_source->running_ = true;

  frame_source->WriteJsonEventMessage("VIRTUAL_DEVICE_START_CAMERA_SESSION");

  frame_source->fd_v4l2_device_ = CF_EXPECT(
      V4l2InitDevice(frame_source->v4l2_device_path_, frame_source->format_,
                     frame_source->frame_width_, frame_source->frame_height_),
      "Error opening v4l2 device");

  CF_EXPECT(frame_source->fd_v4l2_device_->IsOpen(),
            "Error: fd_v4l2_device_->IsOpen() failed");

  ALOGI("%s: successful v4l2 device open.", __FUNCTION__);

  return frame_source;
}

void VsockFrameSource::Stop() {
  if (running_.exchange(false)) {
    if (reader_thread_.joinable()) {
      reader_thread_.join();
    }
    WriteJsonEventMessage("VIRTUAL_DEVICE_STOP_CAMERA_SESSION");
    connection_ = nullptr;
    fd_v4l2_device_->Close();
  }
}

void VsockFrameSource::WriteFrame(const std::vector<char>& frame,
                                  std::vector<char>& rgb_frame) {
  if (rgb_frame.size() != frame_size_) {
    rgb_frame.resize(frame_size_);
  }
  Yuv2Rgb((unsigned char*)frame.data(), (unsigned char*)rgb_frame.data(),
          frame_width_, frame_height_);
  fd_v4l2_device_->Write((unsigned char*)rgb_frame.data(), frame_size_);
}

bool VsockFrameSource::Running() { return running_; }

bool VsockFrameSource::FramesizeMatches(const std::vector<char>& data) {
  return data.size() == 3 * frame_width_ * frame_height_ / 2;
}

Result<void> VsockFrameSource::VsockReadLoopThreaded() {
  CF_EXPECT(fd_v4l2_device_->IsOpen(), "Error: v4l2_initdevice == 0");

  reader_thread_ = std::thread([this] { VsockReadLoop(); });

  return {};
}

void VsockFrameSource::VsockReadLoop() {
  std::vector<char> frame;
  std::vector<char> next_frame;
  std::vector<char> rgb_frame;

  while (running_.load() && connection_->ReadMessage(next_frame)) {
    if (FramesizeMatches(next_frame)) {
      std::lock_guard<std::mutex> lock(frame_mutex_);
      timestamp_ = systemTime();
      frame.swap(next_frame);
      yuv_frame_updated_.notify_one();
      WriteFrame(frame, rgb_frame);
    } else if (IsBlob(next_frame)) {
    }  // TODO
    else {
      ALOGE("%s: Unexpected data of %zu bytes", __FUNCTION__,
            next_frame.size());
    }
  }
  if (!connection_->IsConnected_Unguarded()) {
    ALOGE("%s: Connection closed - exiting", __FUNCTION__);
    running_ = false;
  }
}

}  // End namespace cuttlefish