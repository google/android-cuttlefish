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

#include "host/frontend/webrtc/libdevice/video_track_source_impl.h"

#include <api/video/video_frame_buffer.h>

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

class VideoFrameWrapper : public webrtc::I420BufferInterface {
 public:
  VideoFrameWrapper(
      std::shared_ptr<::cuttlefish::webrtc_streaming::VideoFrameBuffer>
          frame_buffer)
      : frame_buffer_(frame_buffer) {}
  ~VideoFrameWrapper() override = default;
  // From VideoFrameBuffer
  int width() const override { return frame_buffer_->width(); }
  int height() const override { return frame_buffer_->height(); }

  // From class PlanarYuvBuffer
  int StrideY() const override { return frame_buffer_->StrideY(); }
  int StrideU() const override { return frame_buffer_->StrideU(); }
  int StrideV() const override { return frame_buffer_->StrideV(); }

  // From class PlanarYuv8Buffer
  const uint8_t *DataY() const override { return frame_buffer_->DataY(); }
  const uint8_t *DataU() const override { return frame_buffer_->DataU(); }
  const uint8_t *DataV() const override { return frame_buffer_->DataV(); }

 private:
  std::shared_ptr<::cuttlefish::webrtc_streaming::VideoFrameBuffer>
      frame_buffer_;
};

}  // namespace

VideoTrackSourceImpl::VideoTrackSourceImpl(int width, int height)
    : webrtc::VideoTrackSource(false), width_(width), height_(height) {}

void VideoTrackSourceImpl::OnFrame(std::shared_ptr<VideoFrameBuffer> frame,
                                   int64_t timestamp_us) {
  auto video_frame =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(rtc::scoped_refptr<webrtc::VideoFrameBuffer>(
              new rtc::RefCountedObject<VideoFrameWrapper>(frame)))
          .set_timestamp_us(timestamp_us)
          .build();
  broadcaster_.OnFrame(video_frame);
}

bool VideoTrackSourceImpl::GetStats(Stats *stats) {
  stats->input_height = height_;
  stats->input_width = width_;
  return true;
}

bool VideoTrackSourceImpl::SupportsEncodedOutput() const { return false; }
rtc::VideoSourceInterface<webrtc::VideoFrame> *VideoTrackSourceImpl::source() {
  return &broadcaster_;
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
