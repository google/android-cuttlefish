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

#include "cuttlefish/host/frontend/webrtc/libdevice/video_track_source_impl.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_sink_interface.h"
#include "api/video_track_source_constraints.h"
#include "rtc_base/ref_counted_object.h"

#include "cuttlefish/host/frontend/webrtc/libcommon/abgr_buffer.h"

namespace cuttlefish {
namespace webrtc_streaming {

VideoTrackSourceImpl::VideoTrackSourceImpl(int width, int height)
    : width_(width), height_(height) {}

void VideoTrackSourceImpl::OnFrame(std::shared_ptr<VideoFrameBuffer> frame,
                                   int64_t timestamp_us) {
  // Ensure strictly monotonic timestamps at millisecond resolution to prevent
  // WebRTC from dropping frames with "Same/old NTP timestamp" errors, since
  // WebRTC computes NTP timestamps from render_time_ms() (timestamp_us / 1000).
  constexpr int64_t kUsPerMs = 1000;
  {
    std::lock_guard<std::mutex> lock(timestamp_mutex_);
    if (timestamp_us / kUsPerMs <= last_timestamp_us_ / kUsPerMs) {
      timestamp_us = last_timestamp_us_ + kUsPerMs;
    }
    last_timestamp_us_ = timestamp_us;
  }

  const webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer(
      new webrtc::RefCountedObject<AbgrBuffer>(std::move(frame)));

  const webrtc::VideoFrame video_frame = webrtc::VideoFrame::Builder()
                                             .set_video_frame_buffer(buffer)
                                             .set_timestamp_us(timestamp_us)
                                             .build();
  broadcaster_.OnFrame(video_frame);
}

VideoTrackSourceImpl::SourceState VideoTrackSourceImpl::state() const {
  return SourceState::kLive;
}

bool VideoTrackSourceImpl::remote() const { return false; }

void VideoTrackSourceImpl::RegisterObserver(
    webrtc::ObserverInterface* /*observer*/) {}

void VideoTrackSourceImpl::UnregisterObserver(
    webrtc::ObserverInterface* /*observer*/) {}

void VideoTrackSourceImpl::AddOrUpdateSink(
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
    const webrtc::VideoSinkWants& wants) {
  broadcaster_.AddOrUpdateSink(sink, wants);
}

void VideoTrackSourceImpl::RemoveSink(
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
  broadcaster_.RemoveSink(sink);
}

bool VideoTrackSourceImpl::is_screencast() const { return false; }

std::optional<bool> VideoTrackSourceImpl::needs_denoising() const {
  return std::nullopt;
}

bool VideoTrackSourceImpl::GetStats(Stats* stats) {
  stats->input_height = height_;
  stats->input_width = width_;
  return true;
}

bool VideoTrackSourceImpl::SupportsEncodedOutput() const { return false; }

void VideoTrackSourceImpl::ProcessConstraints(
    const webrtc::VideoTrackSourceConstraints& constraints) {
  broadcaster_.ProcessConstraints(constraints);
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
