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

#include "cuttlefish/host/frontend/webrtc/display_handler.h"

#include <fcntl.h>
#include <libyuv.h>
#include <stdint.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "drm/drm_fourcc.h"

#include "cuttlefish/host/frontend/webrtc/cvd_abgr_video_frame_buffer.h"
#include "cuttlefish/host/frontend/webrtc/libdevice/streamer.h"
#include "cuttlefish/host/libs/screen_connector/composition_manager.h"
#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"
#include "cuttlefish/host/libs/wayland/wayland_server.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"

namespace cuttlefish {

DisplayHandler::DisplayHandler(
    webrtc_streaming::Streamer& streamer, ScreenshotHandler& screenshot_handler,
    int wayland_socket_fd, bool wayland_frames_are_rgba,
    std::optional<std::unique_ptr<CompositionManager>> composition_manager)
    : composition_manager_(std::move(composition_manager)),
      streamer_(streamer),
      screenshot_handler_(screenshot_handler) {
  // Initialize the thread after the rest of the class
  frame_repeater_ = std::thread([this]() { RepeatFramesPeriodically(); });

  const int wayland_fd = fcntl(wayland_socket_fd, F_DUPFD_CLOEXEC, 3);
  CHECK(wayland_fd != -1) << "Unable to dup server, errno " << errno;
  close(wayland_socket_fd);

  wayland_server_ = std::make_unique<wayland::WaylandServer>(
      wayland_fd, wayland_frames_are_rgba);
  wayland_server_->SetDisplayEventCallback([this](const DisplayEvent& event) {
    std::visit(
        [this](auto&& e) {
          using T = std::decay_t<decltype(e)>;
          if constexpr (std::is_same_v<DisplayCreatedEvent, T>) {
            VLOG(1) << "Display:" << e.display_number << " created "
                    << " w:" << e.display_width << " h:" << e.display_height;

            const auto display_number = e.display_number;
            const std::string display_id =
                "display_" + std::to_string(e.display_number);
            auto display = streamer_.AddDisplay(display_id, e.display_width,
                                                e.display_height, 160, true);
            if (!display) {
              LOG(ERROR) << "Failed to create display.";
              return;
            }

            std::lock_guard<std::mutex> lock(send_mutex_);
            display_sinks_[display_number] = display;
            if (composition_manager_.has_value()) {
              composition_manager_.value()->OnDisplayCreated(e);
            }
          } else if constexpr (std::is_same_v<DisplayDestroyedEvent, T>) {
            VLOG(1) << "Display:" << e.display_number << " destroyed.";

            const auto display_number = e.display_number;
            const auto display_id =
                "display_" + std::to_string(e.display_number);
            std::lock_guard<std::mutex> lock(send_mutex_);
            display_sinks_.erase(display_number);
            streamer_.RemoveDisplay(display_id);
          } else {
            static_assert(false, "Unhandled display event.");
          }
        },
        event);
  });
  wayland_server_->SetFrameCallback(
      [this](std::uint32_t display_number, std::uint32_t frame_width,
             std::uint32_t frame_height, std::uint32_t frame_fourcc_format,
             std::uint32_t frame_stride_bytes, std::uint8_t* frame_pixels) {
        if (composition_manager_.has_value()) {
          composition_manager_.value()->OnFrame(
              display_number, frame_width, frame_height, frame_fourcc_format,
              frame_stride_bytes, frame_pixels);
        }
        if (frame_fourcc_format != DRM_FORMAT_ARGB8888 &&
            frame_fourcc_format != DRM_FORMAT_XRGB8888 &&
            frame_fourcc_format != DRM_FORMAT_ABGR8888 &&
            frame_fourcc_format != DRM_FORMAT_XBGR8888) {
          LOG(ERROR) << "Unhandled frame format: " << frame_fourcc_format;
          return;
        }
        std::shared_ptr<VideoFrameBuffer> buffer =
            std::make_shared<CvdAbgrVideoFrameBuffer>(
                frame_width, frame_height, frame_fourcc_format,
                frame_stride_bytes, frame_pixels);
        {
          std::lock_guard<std::mutex> lock(last_buffers_mutex_);
          display_last_buffers_[display_number] =
              std::make_shared<BufferInfo>(BufferInfo{
                  .last_sent_time_stamp = std::chrono::system_clock::now(),
                  .buffer = buffer,
              });
        }
        SendLastFrame(display_number);
      });
}

DisplayHandler::~DisplayHandler() {
  // Stop the frame callbacks before tearing down the state they use.
  wayland_server_.reset();
  {
    std::lock_guard lock(repeater_state_mutex_);
    repeater_state_ = RepeaterState::STOPPED;
    repeater_state_condvar_.notify_one();
  }
  frame_repeater_.join();
}

void DisplayHandler::SendLastFrame(std::optional<uint32_t> display_number) {
  std::map<uint32_t, std::shared_ptr<BufferInfo>> buffers;
  {
    std::lock_guard<std::mutex> lock(last_buffers_mutex_);
    if (display_number) {
      // Resend the last buffer for a single display.
      auto last_buffer_it = display_last_buffers_.find(*display_number);
      if (last_buffer_it == display_last_buffers_.end()) {
        return;
      }
      auto& last_buffer_info = last_buffer_it->second;
      if (!last_buffer_info) {
        return;
      }
      auto& last_buffer = last_buffer_info->buffer;
      if (!last_buffer) {
        return;
      }
      buffers[*display_number] = last_buffer_info;
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
  SendBuffers(buffers);
}

void DisplayHandler::SendBuffers(
    std::map<uint32_t, std::shared_ptr<BufferInfo>> buffers) {
  // SendBuffers can be called from multiple threads simultaneously, locking
  // here avoids injecting frames with the timestamps in the wrong order and
  // protects writing the BufferInfo timestamps.
  std::lock_guard<std::mutex> lock(send_mutex_);
  auto time_stamp = std::chrono::system_clock::now();
  int64_t time_stamp_since_epoch =
      std::chrono::duration_cast<std::chrono::microseconds>(
          time_stamp.time_since_epoch())
          .count();

  for (const auto& [display_number, buffer_info] : buffers) {
    screenshot_handler_.OnFrame(display_number, buffer_info->buffer);

    auto it = display_sinks_.find(display_number);
    if (it != display_sinks_.end()) {
      it->second->OnFrame(buffer_info->buffer, time_stamp_since_epoch);
      buffer_info->last_sent_time_stamp = time_stamp;
    }
  }
}

void DisplayHandler::RepeatFramesPeriodically() {
  // SendBuffers can be called from multiple threads simultaneously, locking
  // here avoids injecting frames with the timestamps in the wrong order and
  // protects writing the BufferInfo timestamps.
  const std::chrono::milliseconds kRepeatingInterval(20);
  auto next_send = std::chrono::system_clock::now() + kRepeatingInterval;
  while (true) {
    {
      std::unique_lock lock(repeater_state_mutex_);
      if (repeater_state_ == RepeaterState::STOPPED) {
        break;
      }
      if (num_active_clients_ > 0) {
        bool stopped =
            repeater_state_condvar_.wait_until(lock, next_send, [this]() {
              // Wait until time interval completes or asked to stop. Continue
              // waiting even if the number of active clients drops to 0.
              return repeater_state_ == RepeaterState::STOPPED;
            });
        if (stopped || num_active_clients_ == 0) {
          continue;
        }
      } else {
        repeater_state_condvar_.wait(lock, [this]() {
          // Wait until asked to stop or have clients
          return repeater_state_ == RepeaterState::STOPPED ||
                 num_active_clients_ > 0;
        });
        // Need to break the loop if stopped or wait for the interval if have
        // clients.
        continue;
      }
    }

    std::map<uint32_t, std::shared_ptr<BufferInfo>> buffers;
    {
      std::lock_guard last_buffers_lock(last_buffers_mutex_);
      auto time_stamp = std::chrono::system_clock::now();

      for (auto& [display_number, buffer_info] : display_last_buffers_) {
        if (time_stamp >
            buffer_info->last_sent_time_stamp + kRepeatingInterval) {
          if (composition_manager_.has_value() && buffer_info->buffer) {
            composition_manager_.value()->ComposeFrame(display_number,
                                                       buffer_info->buffer);
          }
          buffers[display_number] = buffer_info;
        }
      }
    }
    SendBuffers(buffers);
    {
      std::lock_guard last_buffers_lock(last_buffers_mutex_);
      next_send = std::chrono::system_clock::now() + kRepeatingInterval;
      for (const auto& [_, buffer_info] : display_last_buffers_) {
        next_send = std::min(
            next_send, buffer_info->last_sent_time_stamp + kRepeatingInterval);
      }
    }
  }
}

void DisplayHandler::AddDisplayClient() {
  std::lock_guard lock(repeater_state_mutex_);
  if (++num_active_clients_ == 1) {
    repeater_state_condvar_.notify_one();
  };
}

void DisplayHandler::RemoveDisplayClient() {
  std::lock_guard lock(repeater_state_mutex_);
  --num_active_clients_;
}

}  // namespace cuttlefish
