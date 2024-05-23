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

#include "host/frontend/webrtc/display_handler.h"

#include <chrono>
#include <functional>
#include <memory>

#include <drm/drm_fourcc.h>
#include <libyuv.h>

#include "host/frontend/webrtc/libdevice/streamer.h"

namespace cuttlefish {

DisplayHandler::DisplayHandler(webrtc_streaming::Streamer& streamer,
                               int wayland_socket_fd,
                               bool wayland_frames_are_rgba)
    : streamer_(streamer) {
  int wayland_fd = fcntl(wayland_socket_fd, F_DUPFD_CLOEXEC, 3);
  CHECK(wayland_fd != -1) << "Unable to dup server, errno " << errno;
  close(wayland_socket_fd);
  wayland_server_ = std::make_unique<wayland::WaylandServer>(
      wayland_fd, wayland_frames_are_rgba);
  wayland_server_->SetDisplayEventCallback([this](const DisplayEvent& event) {
    std::visit(
        [this](auto&& e) {
          using T = std::decay_t<decltype(e)>;
          if constexpr (std::is_same_v<DisplayCreatedEvent, T>) {
            LOG(VERBOSE) << "Display:" << e.display_number << " created "
                         << " w:" << e.display_width
                         << " h:" << e.display_height;

            const auto display_number = e.display_number;
            const std::string display_id =
                "display_" + std::to_string(e.display_number);
            auto display = streamer_.AddDisplay(display_id, e.display_width,
                                                e.display_height, 160, true);
            if (!display) {
              LOG(ERROR) << "Failed to create display.";
              return;
            }

            display_sinks_[display_number] = display;
          } else if constexpr (std::is_same_v<DisplayDestroyedEvent, T>) {
            LOG(VERBOSE) << "Display:" << e.display_number << " destroyed.";

            const auto display_number = e.display_number;
            const auto display_id =
                "display_" + std::to_string(e.display_number);
            streamer_.RemoveDisplay(display_id);
            display_sinks_.erase(display_number);
          } else {
            static_assert("Unhandled display event.");
          }
        },
        event);
  });
  wayland_server_->SetFrameCallback([this](
                                        std::uint32_t display_number,       //
                                        std::uint32_t frame_width,          //
                                        std::uint32_t frame_height,         //
                                        std::uint32_t frame_fourcc_format,  //
                                        std::uint32_t frame_stride_bytes,   //
                                        std::uint8_t* frame_pixels) {
    auto buf = std::make_shared<CvdVideoFrameBuffer>(frame_width, frame_height);
    if (frame_fourcc_format == DRM_FORMAT_ARGB8888 ||
        frame_fourcc_format == DRM_FORMAT_XRGB8888) {
      libyuv::ARGBToI420(frame_pixels, frame_stride_bytes, buf->DataY(),
                         buf->StrideY(), buf->DataU(), buf->StrideU(),
                         buf->DataV(), buf->StrideV(), frame_width,
                         frame_height);
    } else if (frame_fourcc_format == DRM_FORMAT_ABGR8888 ||
               frame_fourcc_format == DRM_FORMAT_XBGR8888) {
      libyuv::ABGRToI420(frame_pixels, frame_stride_bytes, buf->DataY(),
                         buf->StrideY(), buf->DataU(), buf->StrideU(),
                         buf->DataV(), buf->StrideV(), frame_width,
                         frame_height);
    } else {
      LOG(ERROR) << "Unhandled frame format: " << frame_fourcc_format;
      return;
    }

    {
      std::lock_guard<std::mutex> lock(last_buffer_mutex_);
      display_last_buffers_[display_number] =
          std::static_pointer_cast<webrtc_streaming::VideoFrameBuffer>(buf);
    }

    SendLastFrame(display_number);
  });
}

void DisplayHandler::SendLastFrame(std::optional<uint32_t> display_number) {
  std::map<uint32_t, std::shared_ptr<webrtc_streaming::VideoFrameBuffer>>
      buffers;
  {
    std::lock_guard<std::mutex> lock(last_buffer_mutex_);
    if (display_number) {
      // Resend the last buffer for a single display.
      auto last_buffer_it = display_last_buffers_.find(*display_number);
      if (last_buffer_it == display_last_buffers_.end()) {
        return;
      }
      auto& last_buffer = last_buffer_it->second;
      if (!last_buffer) {
        return;
      }
      buffers[*display_number] = last_buffer;
    } else {
      // Resend the last buffer for all displays.
      buffers = display_last_buffers_;
    }
  }
  if (buffers.empty()) {
    // If a connection request arrives before the first frame is available don't
    // send any frame.
    return;
  }
  {
    // SendLastFrame can be called from multiple threads simultaneously, locking
    // here avoids injecting frames with the timestamps in the wrong order.
    std::lock_guard<std::mutex> lock(next_frame_mutex_);
    int64_t time_stamp =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    for (const auto& [display_number, buffer] : buffers) {
      auto it = display_sinks_.find(display_number);
      if (it != display_sinks_.end()) {
        it->second->OnFrame(buffer, time_stamp);
      }
    }
  }
}

}  // namespace cuttlefish
