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

#include <libyuv.h>

namespace cuttlefish {
DisplayHandler::DisplayHandler(
    std::shared_ptr<webrtc_streaming::VideoSink> display_sink,
    ScreenConnector* screen_connector)
    : display_sink_(display_sink), screen_connector_(screen_connector) {}

[[noreturn]] void DisplayHandler::Loop() {
  std::uint32_t frame_num = 0;
  for (;;) {
    auto have_frame = screen_connector_->OnFrameAfter(
        frame_num, [&frame_num, this](std::uint32_t fn, std::uint8_t* frame) {
          frame_num = fn;
          std::shared_ptr<CvdVideoFrameBuffer> buffer(
              new CvdVideoFrameBuffer(screen_connector_->ScreenWidth(),
                                      screen_connector_->ScreenHeight()));
          libyuv::ABGRToI420(frame, screen_connector_->ScreenStride(),
                             buffer->DataY(), buffer->StrideY(),
                             buffer->DataU(), buffer->StrideU(),
                             buffer->DataV(), buffer->StrideV(),
                             screen_connector_->ScreenWidth(),
                             screen_connector_->ScreenHeight());
          {
            std::lock_guard<std::mutex> lock(last_buffer_mutex_);
            last_buffer_ =
                std::static_pointer_cast<webrtc_streaming::VideoFrameBuffer>(buffer);
          }
        });
    if (have_frame) {
      SendLastFrame();
    }
  }
}

void DisplayHandler::SendLastFrame() {
  std::shared_ptr<webrtc_streaming::VideoFrameBuffer> buffer;
  {
    std::lock_guard<std::mutex> lock(last_buffer_mutex_);
    buffer = last_buffer_;
  }
  if (!buffer) {
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
    display_sink_->OnFrame(buffer, time_stamp);
  }
}

void DisplayHandler::IncClientCount() {
  client_count_++;
  if (client_count_ == 1) {
    screen_connector_->ReportClientsConnected(true);
  }
}

void DisplayHandler::DecClientCount() {
  client_count_--;
  if (client_count_ == 0) {
    screen_connector_->ReportClientsConnected(false);
  }
}

}  // namespace cuttlefish
