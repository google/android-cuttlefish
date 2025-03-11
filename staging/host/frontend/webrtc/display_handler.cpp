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
#include <memory>

#include <drm/drm_fourcc.h>
#include <libyuv.h>

#include "host/frontend/webrtc/libdevice/streamer.h"

namespace cuttlefish {

DisplayHandler::DisplayHandler(webrtc_streaming::Streamer& streamer,
                               ScreenConnector& screen_connector)
    : streamer_(streamer),
      screen_connector_(screen_connector),
      frame_repeater_([this]() { RepeatFramesPeriodically(); }) {
  screen_connector_.SetCallback(GetScreenConnectorCallback());
  screen_connector_.SetDisplayEventCallback([this](const DisplayEvent& event) {
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
}

DisplayHandler::~DisplayHandler() {
  {
    std::lock_guard lock(repeater_state_mutex_);
    repeater_state_ = RepeaterState::STOPPED;
    repeater_state_condvar_.notify_one();
  }
  frame_repeater_.join();
}

DisplayHandler::GenerateProcessedFrameCallback
DisplayHandler::GetScreenConnectorCallback() {
  // only to tell the producer how to create a ProcessedFrame to cache into the
  // queue
  DisplayHandler::GenerateProcessedFrameCallback callback =
      [](std::uint32_t display_number, std::uint32_t frame_width,
         std::uint32_t frame_height, std::uint32_t frame_fourcc_format,
         std::uint32_t frame_stride_bytes, std::uint8_t* frame_pixels,
         WebRtcScProcessedFrame& processed_frame) {
        processed_frame.display_number_ = display_number;
        processed_frame.buf_ =
            std::make_unique<CvdVideoFrameBuffer>(frame_width, frame_height);
        if (frame_fourcc_format == DRM_FORMAT_ARGB8888 ||
            frame_fourcc_format == DRM_FORMAT_XRGB8888) {
          libyuv::ARGBToI420(
              frame_pixels, frame_stride_bytes, processed_frame.buf_->DataY(),
              processed_frame.buf_->StrideY(), processed_frame.buf_->DataU(),
              processed_frame.buf_->StrideU(), processed_frame.buf_->DataV(),
              processed_frame.buf_->StrideV(), frame_width, frame_height);
          processed_frame.is_success_ = true;
        } else if (frame_fourcc_format == DRM_FORMAT_ABGR8888 ||
                   frame_fourcc_format == DRM_FORMAT_XBGR8888) {
          libyuv::ABGRToI420(
              frame_pixels, frame_stride_bytes, processed_frame.buf_->DataY(),
              processed_frame.buf_->StrideY(), processed_frame.buf_->DataU(),
              processed_frame.buf_->StrideU(), processed_frame.buf_->DataV(),
              processed_frame.buf_->StrideV(), frame_width, frame_height);
          processed_frame.is_success_ = true;
        } else {
          processed_frame.is_success_ = false;
        }
      };
  return callback;
}

[[noreturn]] void DisplayHandler::Loop() {
  for (;;) {
    auto processed_frame = screen_connector_.OnNextFrame();

    std::shared_ptr<CvdVideoFrameBuffer> buffer =
        std::move(processed_frame.buf_);

    const uint32_t display_number = processed_frame.display_number_;
    {
      std::lock_guard<std::mutex> lock(last_buffers_mutex_);
      display_last_buffers_[display_number] =
          std::make_shared<BufferInfo>(BufferInfo{
              .last_sent_time_stamp = std::chrono::system_clock::now(),
              .buffer =
                  std::static_pointer_cast<webrtc_streaming::VideoFrameBuffer>(
                      buffer),
          });
    }
    if (processed_frame.is_success_) {
      SendLastFrame(display_number);
    }
  }
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
  std::unique_lock lock(repeater_state_mutex_);
  while (repeater_state_ != RepeaterState::STOPPED) {
    if (repeater_state_ == RepeaterState::REPEATING) {
      repeater_state_condvar_.wait_until(lock, next_send);
    } else {
      repeater_state_condvar_.wait(lock);
    }
    if (repeater_state_ != RepeaterState::REPEATING) {
      continue;
    }

    std::map<uint32_t, std::shared_ptr<BufferInfo>> buffers;
    {
      std::lock_guard last_buffers_lock(last_buffers_mutex_);
      auto time_stamp = std::chrono::system_clock::now();

      for (auto& [display_number, buffer_info] : display_last_buffers_) {
        if (time_stamp >
            buffer_info->last_sent_time_stamp + kRepeatingInterval) {
          buffers[display_number] = buffer_info;
        }
      }
    }
    SendBuffers(buffers);
    {
      std::lock_guard last_buffers_lock(last_buffers_mutex_);
      for (const auto& [_, buffer_info] : display_last_buffers_) {
        next_send = std::min(
            next_send, buffer_info->last_sent_time_stamp + kRepeatingInterval);
      }
    }
  }
}

void DisplayHandler::AddDisplayClient() {
  std::lock_guard lock(repeater_state_mutex_);
  ++num_active_clients_;
  if (num_active_clients_ == 1) {
    repeater_state_ = RepeaterState::REPEATING;
    repeater_state_condvar_.notify_one();
  }
}

void DisplayHandler::RemoveDisplayClient() {
  std::lock_guard lock(repeater_state_mutex_);
  --num_active_clients_;
  if (num_active_clients_ == 0) {
    repeater_state_ = RepeaterState::PAUSED;
    repeater_state_condvar_.notify_one();
  }
}

}  // namespace cuttlefish
