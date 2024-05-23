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

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "host/frontend/webrtc/cvd_video_frame_buffer.h"
#include "host/frontend/webrtc/libdevice/video_sink.h"
#include "host/libs/wayland/wayland_server.h"

namespace cuttlefish {

namespace webrtc_streaming {
class Streamer;
}  // namespace webrtc_streaming

class DisplayHandler {
 public:
  DisplayHandler(webrtc_streaming::Streamer& streamer, int wayland_socket_fd,
                 bool wayland_frames_are_rgba);
  ~DisplayHandler() = default;

  // If std::nullopt, send last frame for all displays.
  void SendLastFrame(std::optional<uint32_t> display_number);

 private:
  std::unique_ptr<wayland::WaylandServer> wayland_server_;
  std::map<uint32_t, std::shared_ptr<webrtc_streaming::VideoSink>>
      display_sinks_;
  webrtc_streaming::Streamer& streamer_;
  std::map<uint32_t, std::shared_ptr<webrtc_streaming::VideoFrameBuffer>>
      display_last_buffers_;
  std::mutex last_buffer_mutex_;
  std::mutex next_frame_mutex_;
};
}  // namespace cuttlefish
