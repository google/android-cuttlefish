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

#include <libyuv.h>

namespace cuttlefish {
DisplayHandler::DisplayHandler(
    std::shared_ptr<webrtc_streaming::VideoSink> display_sink,
    ScreenConnector& screen_connector)
    : display_sink_(display_sink), screen_connector_(screen_connector) {
  screen_connector_.SetCallback(std::move(GetScreenConnectorCallback()));
}

DisplayHandler::GenerateProcessedFrameCallback DisplayHandler::GetScreenConnectorCallback() {
    // only to tell the producer how to create a ProcessedFrame to cache into the queue
    DisplayHandler::GenerateProcessedFrameCallback callback =
        [](std::uint32_t display_number, std::uint8_t* frame_pixels,
           WebRtcScProcessedFrame& processed_frame) {
          processed_frame.display_number_ = display_number;
          // TODO(171305898): handle multiple displays.
          if (display_number != 0) {
            // BUG 186580833: display_number comes from surface_id in crosvm
            // create_surface from virtio_gpu.rs set_scanout.  We cannot use it as
            // the display number. Either crosvm virtio-gpu is incorrectly ignoring
            // scanout id and instead using a monotonically increasing surface id
            // number as the scanout resource is replaced over time, or frontend code
            // here is incorrectly assuming  surface id == display id.
            display_number = 0;
          }
          const int display_w =
              ScreenConnectorInfo::ScreenWidth(display_number);
          const int display_h =
              ScreenConnectorInfo::ScreenHeight(display_number);
          const int display_stride_bytes =
              ScreenConnectorInfo::ScreenStrideBytes(display_number);
          processed_frame.display_number_ = display_number;
          processed_frame.buf_ =
              std::make_unique<CvdVideoFrameBuffer>(display_w, display_h);
          libyuv::ABGRToI420(
              frame_pixels, display_stride_bytes, processed_frame.buf_->DataY(),
              processed_frame.buf_->StrideY(), processed_frame.buf_->DataU(),
              processed_frame.buf_->StrideU(), processed_frame.buf_->DataV(),
              processed_frame.buf_->StrideV(), display_w, display_h);
          processed_frame.is_success_ = true;
        };
    return callback;
}

[[noreturn]] void DisplayHandler::Loop() {
  for (;;) {
    auto processed_frame = screen_connector_.OnNextFrame();
    // processed_frame has display number from the guest
    {
      std::lock_guard<std::mutex> lock(last_buffer_mutex_);
      std::shared_ptr<CvdVideoFrameBuffer> buffer = std::move(processed_frame.buf_);
      last_buffer_ =
          std::static_pointer_cast<webrtc_streaming::VideoFrameBuffer>(buffer);
    }
    if (processed_frame.is_success_) {
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
    screen_connector_.ReportClientsConnected(true);
  }
}

void DisplayHandler::DecClientCount() {
  client_count_--;
  if (client_count_ == 0) {
    screen_connector_.ReportClientsConnected(false);
  }
}

}  // namespace cuttlefish
