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

#include "host/frontend/webrtc/lib/video_track_source_impl.h"


namespace cuttlefish {
namespace webrtc_streaming {

VideoTrackSourceImpl::VideoTrackSourceImpl(int width, int height)
    : webrtc::VideoTrackSource(false), width_(width), height_(height) {}

void VideoTrackSourceImpl::OnFrame(const webrtc::VideoFrame& frame) {
  broadcaster_.OnFrame(frame);
}

bool VideoTrackSourceImpl::GetStats(Stats* stats) {
  stats->input_height = height_;
  stats->input_width = width_;
  return true;
}

bool VideoTrackSourceImpl::SupportsEncodedOutput() const { return false; }
rtc::VideoSourceInterface<webrtc::VideoFrame>* VideoTrackSourceImpl::source() {
  return &broadcaster_;
}

}}
