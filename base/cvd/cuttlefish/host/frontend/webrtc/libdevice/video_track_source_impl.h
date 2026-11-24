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

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "api/media_stream_interface.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/video_broadcaster.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video_track_source_constraints.h"

#include "cuttlefish/host/frontend/webrtc/libdevice/video_sink.h"

namespace cuttlefish {
namespace webrtc_streaming {

// Video source implementation that bridges Cuttlefish display frames to WebRTC.
//
// Receives video frames from the display compositor (via VideoSink interface)
// and broadcasts them to WebRTC video tracks. Handles both YUV (I420) and
// native RGBA frame formats.
//
// Key features:
//   - Supports native ARGB/ABGR buffers for hardware encoder passthrough
//   - Enforces strictly monotonic timestamps to prevent frame drops
//   - Broadcasts frames to all registered WebRTC sinks
//
// Thread safety: OnFrame(), AddOrUpdateSink(), and RemoveSink() may be called
// from any thread after construction via the thread-safe VideoBroadcaster.
class VideoTrackSourceImpl : public webrtc::VideoTrackSourceInterface {
 public:
  VideoTrackSourceImpl(int width, int height);

  void OnFrame(std::shared_ptr<VideoFrameBuffer> frame, int64_t timestamp_us);

  // MediaSourceInterface implementation
  SourceState state() const override;
  bool remote() const override;

  // NotifierInterface implementation
  void RegisterObserver(webrtc::ObserverInterface* observer) override;
  void UnregisterObserver(webrtc::ObserverInterface* observer) override;

  // VideoSourceInterface implementation
  void AddOrUpdateSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
                       const webrtc::VideoSinkWants& wants) override;
  void RemoveSink(
      webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override;

  // VideoTrackSourceInterface implementation
  bool is_screencast() const override;
  std::optional<bool> needs_denoising() const override;

  // Returns false if no stats are available, e.g, for a remote source, or a
  // source which has not seen its first frame yet.
  //
  // Implementation should avoid blocking.
  bool GetStats(Stats* stats) override;

  bool SupportsEncodedOutput() const override;
  void GenerateKeyFrame() override {}
  void AddEncodedSink(
      webrtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>* sink)
      override {}
  void RemoveEncodedSink(
      webrtc::VideoSinkInterface<webrtc::RecordableEncodedFrame>* sink)
      override {}
  void ProcessConstraints(
      const webrtc::VideoTrackSourceConstraints& constraints) override;

 private:
  const int width_;
  const int height_;
  std::mutex timestamp_mutex_;
  int64_t last_timestamp_us_ = 0;  // Ensure strictly monotonic timestamps
  webrtc::VideoBroadcaster broadcaster_;
};

// Wraps a VideoTrackSourceImpl as an implementation of the VideoSink interface.
// This is needed as the VideoTrackSourceImpl is a reference counted object that
// should only be referenced by rtc::scoped_refptr pointers, but the
// VideoSink interface is not a reference counted object and therefore not
// compatible with that kind of pointers. This class can be referenced by a
// shared pointer and it in turn holds a scoped_refptr to the wrapped object.
class VideoTrackSourceImplSinkWrapper : public VideoSink {
 public:
  virtual ~VideoTrackSourceImplSinkWrapper() = default;

  VideoTrackSourceImplSinkWrapper(
      webrtc::scoped_refptr<VideoTrackSourceImpl> obj)
      : track_source_impl_(obj) {}

  void OnFrame(std::shared_ptr<VideoFrameBuffer> frame,
               int64_t timestamp_us) override {
    track_source_impl_->OnFrame(frame, timestamp_us);
  }

 private:
  webrtc::scoped_refptr<VideoTrackSourceImpl> track_source_impl_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
