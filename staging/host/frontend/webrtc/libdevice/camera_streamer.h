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

#pragma once
#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>
#include <api/video/video_sink_interface.h>
#include <json/json.h>

#include "common/libs/utils/vsock_connection.h"
#include "host/frontend/webrtc/libdevice/camera_controller.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace cuttlefish {
namespace webrtc_streaming {

class CameraStreamer : public rtc::VideoSinkInterface<webrtc::VideoFrame>,
                       public CameraController {
 public:
  CameraStreamer(unsigned int port, unsigned int cid);
  ~CameraStreamer();

  CameraStreamer(const CameraStreamer& other) = delete;
  CameraStreamer& operator=(const CameraStreamer& other) = delete;

  void OnFrame(const webrtc::VideoFrame& frame) override;

  void HandleMessage(const Json::Value& message) override;
  void HandleMessage(const std::vector<char>& message) override;

 private:
  using Resolution = struct {
    int32_t width;
    int32_t height;
  };
  bool ForwardClientMessage(const Json::Value& message);
  Resolution GetResolutionFromSettings(const Json::Value& settings);
  bool VsockSendYUVFrame(const webrtc::I420BufferInterface* frame);
  bool IsConnectionReady();
  void StartReadLoop();
  void Disconnect();
  std::future<bool> pending_connection_;
  VsockClientConnection cvd_connection_;
  std::atomic<Resolution> resolution_;
  std::mutex settings_mutex_;
  std::string settings_buffer_;
  std::mutex frame_mutex_;
  std::mutex onframe_mutex_;
  rtc::scoped_refptr<webrtc::I420Buffer> scaled_frame_;
  unsigned int cid_;
  unsigned int port_;
  std::thread reader_thread_;
  std::atomic<bool> camera_session_active_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
