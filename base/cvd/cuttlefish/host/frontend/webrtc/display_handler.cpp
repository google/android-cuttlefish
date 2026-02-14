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

#include <stdint.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <drm/drm_fourcc.h>
#include <libyuv.h>
#include "absl/log/log.h"

#include "cuttlefish/host/frontend/webrtc/libdevice/streamer.h"
#include "cuttlefish/host/libs/screen_connector/composition_manager.h"
#include "cuttlefish/host/libs/screen_connector/video_frame_buffer.h"

namespace cuttlefish {

DisplayHandler::DisplayHandler(
    webrtc_streaming::Streamer& streamer, ScreenshotHandler& screenshot_handler,
    ScreenConnector& screen_connector,
    std::optional<std::unique_ptr<CompositionManager>> composition_manager)
    : composition_manager_(std::move(composition_manager)),
      streamer_(streamer),
      screenshot_handler_(screenshot_handler),
      screen_connector_(screen_connector) {
  // Initialize the thread after the rest of the class
  frame_repeater_ = std::thread([this]() { RepeatFramesPeriodically(); });
  screen_connector_.SetCallback(GetScreenConnectorCallback());
  screen_connector_.SetDisplayEventCallback([this](const DisplayEvent& event) {
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
  auto& composition_manager = composition_manager_;
  DisplayHandler::GenerateProcessedFrameCallback callback =
      [&composition_manager](
          uint32_t display_number, uint32_t frame_width, uint32_t frame_height,
          uint32_t frame_fourcc_format, uint32_t frame_stride_bytes,
          uint8_t* frame_pixels, WebRtcScProcessedFrame& processed_frame) {
        processed_frame.display_number_ = display_number;
        processed_frame.buf_ =
            std::make_unique<CvdVideoFrameBuffer>(frame_width, frame_height);
        if (composition_manager.has_value()) {
          composition_manager.value()->OnFrame(
              display_number, frame_width, frame_height, frame_fourcc_format,
              frame_stride_bytes, frame_pixels);
        }
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
              .buffer = std::static_pointer_cast<VideoFrameBuffer>(buffer),
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
          if (composition_manager_.has_value()) {
            composition_manager_.value()->ComposeFrame(
                display_number, std::static_pointer_cast<CvdVideoFrameBuffer>(
                                    buffer_info->buffer));
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
