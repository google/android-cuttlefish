/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "cuttlefish/host/frontend/webrtc/libdevice/video_sink.h"
#include "cuttlefish/host/frontend/webrtc/screenshot_handler.h"
#include "cuttlefish/host/libs/screen_connector/ring_buffer_manager.h"
#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"
#include "cuttlefish/host/libs/wayland/wayland_server.h"

namespace cuttlefish {
class CompositionManager;

namespace webrtc_streaming {
class Streamer;
}  // namespace webrtc_streaming

class DisplayHandler {
 public:
  DisplayHandler(
      webrtc_streaming::Streamer& streamer,
      ScreenshotHandler& screenshot_handler, int wayland_socket_fd,
      bool wayland_frames_are_rgba,
      std::optional<std::unique_ptr<CompositionManager>> composition_manager);
  ~DisplayHandler();

  // If std::nullopt, send last frame for all displays.
  void SendLastFrame(std::optional<uint32_t> display_number);

  void AddDisplayClient();
  void RemoveDisplayClient();

 private:
  struct BufferInfo {
    std::chrono::system_clock::time_point last_sent_time_stamp;
    std::shared_ptr<VideoFrameBuffer> buffer;
  };
  enum class RepeaterState {
    RUNNING,
    STOPPED,
  };
  void SendBuffers(std::map<uint32_t, std::shared_ptr<BufferInfo>> buffers);
  void RepeatFramesPeriodically();

  std::optional<std::unique_ptr<CompositionManager>> composition_manager_;
  std::map<uint32_t, std::shared_ptr<webrtc_streaming::VideoSink>>
      display_sinks_;
  webrtc_streaming::Streamer& streamer_;
  ScreenshotHandler& screenshot_handler_;
  std::unique_ptr<wayland::WaylandServer> wayland_server_;
  std::map<uint32_t, std::shared_ptr<BufferInfo>> display_last_buffers_;
  std::mutex last_buffers_mutex_;
  std::mutex send_mutex_;
  std::thread frame_repeater_;
  // Protected by repeater_state_mutex
  RepeaterState repeater_state_ = RepeaterState::RUNNING;
  // Protected by repeater_state_mutex
  int num_active_clients_ = 0;
  std::mutex repeater_state_mutex_;
  std::condition_variable repeater_state_condvar_;
};
}  // namespace cuttlefish
