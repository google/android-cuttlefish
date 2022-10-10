/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include "camera_streamer.h"

#include <android-base/logging.h>
#include <chrono>
#include "common/libs/utils/vsock_connection.h"

namespace cuttlefish {
namespace webrtc_streaming {

CameraStreamer::CameraStreamer(unsigned int port, unsigned int cid)
    : cid_(cid), port_(port), camera_session_active_(false) {}

CameraStreamer::~CameraStreamer() { Disconnect(); }

// We are getting frames from the client so try forwarding those to the CVD
void CameraStreamer::OnFrame(const webrtc::VideoFrame& client_frame) {
  std::lock_guard<std::mutex> lock(onframe_mutex_);
  if (!cvd_connection_.IsConnected() && !pending_connection_.valid()) {
    // Start new connection
    pending_connection_ = cvd_connection_.ConnectAsync(port_, cid_);
    return;
  } else if (pending_connection_.valid()) {
    if (!IsConnectionReady()) {
      return;
    }
    std::lock_guard<std::mutex> lock(settings_mutex_);
    if (!cvd_connection_.WriteMessage(settings_buffer_)) {
      LOG(ERROR) << "Failed writing camera settings:";
      return;
    }
    StartReadLoop();
    LOG(INFO) << "Connected!";
  }
  auto resolution = resolution_.load();
  if (resolution.height <= 0 || resolution.width <= 0 ||
      !camera_session_active_.load()) {
    // Nobody is receiving frames or we don't have a valid resolution that is
    // necessary for potential frame scaling
    return;
  }
  auto frame = client_frame.video_frame_buffer()->ToI420().get();
  if (frame->width() != resolution.width ||
      frame->height() != resolution.height) {
    // incoming resolution does not match with the resolution we
    // have communicated to the CVD - scaling required
    if (!scaled_frame_ || resolution.width != scaled_frame_->width() ||
        resolution.height != scaled_frame_->height()) {
      scaled_frame_ =
          webrtc::I420Buffer::Create(resolution.width, resolution.height);
    }
    scaled_frame_->CropAndScaleFrom(*frame);
    frame = scaled_frame_.get();
  }
  if (!VsockSendYUVFrame(frame)) {
    LOG(ERROR) << "Sending frame over vsock failed";
  }
}

// Handle message json coming from client
void CameraStreamer::HandleMessage(const Json::Value& message) {
  auto command = message["command"].asString();
  if (command == "camera_settings") {
    // save local copy of resolution that is required for frame scaling
    resolution_ = GetResolutionFromSettings(message);
    Json::StreamWriterBuilder factory;
    std::string new_settings = Json::writeString(factory, message);
    if (!settings_buffer_.empty() && new_settings != settings_buffer_) {
      // Settings have changed - disconnect
      // Next incoming frames will trigger re-connection
      Disconnect();
    }
    std::lock_guard<std::mutex> lock(settings_mutex_);
    settings_buffer_ = new_settings;
    LOG(INFO) << "New camera settings received:" << new_settings;
  }
}

// Handle binary blobs coming from client
void CameraStreamer::HandleMessage(const std::vector<char>& message) {
  LOG(INFO) << "Pass through " << message.size() << "bytes";
  std::lock_guard<std::mutex> lock(frame_mutex_);
  cvd_connection_.WriteMessage(message);
}

CameraStreamer::Resolution CameraStreamer::GetResolutionFromSettings(
    const Json::Value& settings) {
  return {.width = settings["width"].asInt(),
          .height = settings["height"].asInt()};
}

bool CameraStreamer::VsockSendYUVFrame(
    const webrtc::I420BufferInterface* frame) {
  int32_t size = frame->width() * frame->height() +
                 2 * frame->ChromaWidth() * frame->ChromaHeight();
  const char* y = reinterpret_cast<const char*>(frame->DataY());
  const char* u = reinterpret_cast<const char*>(frame->DataU());
  const char* v = reinterpret_cast<const char*>(frame->DataV());
  auto chroma_width = frame->ChromaWidth();
  auto chroma_height = frame->ChromaHeight();
  std::lock_guard<std::mutex> lock(frame_mutex_);
  return cvd_connection_.Write(size) &&
         cvd_connection_.WriteStrides(y, frame->width(), frame->height(),
                                      frame->StrideY()) &&
         cvd_connection_.WriteStrides(u, chroma_width, chroma_height,
                                      frame->StrideU()) &&
         cvd_connection_.WriteStrides(v, chroma_width, chroma_height,
                                      frame->StrideV());
}

bool CameraStreamer::IsConnectionReady() {
  if (!pending_connection_.valid()) {
    return cvd_connection_.IsConnected();
  } else if (pending_connection_.wait_for(std::chrono::seconds(0)) !=
             std::future_status::ready) {
    // Still waiting for connection
    return false;
  } else if (settings_buffer_.empty()) {
    // connection is ready but we have not yet received client
    // camera settings
    return false;
  }
  return pending_connection_.get();
}

void CameraStreamer::StartReadLoop() {
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
  reader_thread_ = std::thread([this] {
    while (cvd_connection_.IsConnected()) {
      static constexpr auto kEventKey = "event";
      static constexpr auto kMessageStart =
          "VIRTUAL_DEVICE_START_CAMERA_SESSION";
      static constexpr auto kMessageStop = "VIRTUAL_DEVICE_STOP_CAMERA_SESSION";
      auto json_value = cvd_connection_.ReadJsonMessage();
      if (json_value[kEventKey] == kMessageStart) {
        camera_session_active_ = true;
      } else if (json_value[kEventKey] == kMessageStop) {
        camera_session_active_ = false;
      }
      if (!json_value.empty()) {
        SendMessage(json_value);
      }
    }
    LOG(INFO) << "Exit reader thread";
  });
}

void CameraStreamer::Disconnect() {
  cvd_connection_.Disconnect();
  if (reader_thread_.joinable()) {
    reader_thread_.join();
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
