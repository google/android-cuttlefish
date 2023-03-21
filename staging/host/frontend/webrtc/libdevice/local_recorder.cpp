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

#include "host/frontend/webrtc/libdevice/local_recorder.h"

#include <atomic>
#include <chrono>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

#include <android-base/logging.h>
#include <api/media_stream_interface.h>
#include <api/rtp_parameters.h>
#include <api/task_queue/default_task_queue_factory.h>
#include <api/video/builtin_video_bitrate_allocator_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <api/video_codecs/video_encoder.h>
#include <mkvmuxer/mkvmuxer.h>
#include <mkvmuxer/mkvwriter.h>
#include <system_wrappers/include/clock.h>

namespace cuttlefish {
namespace webrtc_streaming {

constexpr double kRtpTicksPerSecond = 90000.;
constexpr double kRtpTicksPerMs = kRtpTicksPerSecond / 1000.;
constexpr double kRtpTicksPerUs = kRtpTicksPerMs / 1000.;
constexpr double kRtpTicksPerNs = kRtpTicksPerUs / 1000.;

class LocalRecorder::Display
    : public webrtc::EncodedImageCallback
    , public rtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
  Display(LocalRecorder::Impl& impl);

  void EncoderLoop();
  void Stop();

  // VideoSinkInterface
  virtual void OnFrame(const webrtc::VideoFrame& frame) override;

  // EncodedImageCallback
  virtual webrtc::EncodedImageCallback::Result OnEncodedImage(
      const webrtc::EncodedImage& encoded_image,
      const webrtc::CodecSpecificInfo* codec_specific_info) override;

  LocalRecorder::Impl& impl_;
  std::shared_ptr<webrtc::VideoTrackSourceInterface> source_;
  std::unique_ptr<webrtc::VideoEncoder> video_encoder_;
  uint64_t video_track_number_;

  // TODO(schuffelen): Use a WebRTC task queue?
  std::thread encoder_thread_;
  std::condition_variable encoder_queue_signal_;
  std::mutex encode_queue_mutex_;
  std::list<webrtc::VideoFrame> encode_queue_;
  std::atomic_bool encoder_running_ = true;
};

class LocalRecorder::Impl {
public:
  mkvmuxer::MkvWriter file_writer_;
  mkvmuxer::Segment segment_;
  std::unique_ptr<webrtc::VideoEncoderFactory> encoder_factory_;
  std::mutex mkv_mutex_;
  std::vector<std::unique_ptr<Display>> displays_;
};

/* static */
std::unique_ptr<LocalRecorder> LocalRecorder::Create(
    const std::string& filename) {
  std::unique_ptr<Impl> impl(new Impl());

  if (!impl->file_writer_.Open(filename.c_str())) {
    LOG(ERROR) << "Failed to open \"" << filename << "\" to write a webm";
    return {};
  }

  if (!impl->segment_.Init(&impl->file_writer_)) {
    LOG(ERROR) << "Failed to initialize the mkvkmuxer segment";
    return {};
  }

  impl->segment_.AccurateClusterDuration(true);
  impl->segment_.set_estimate_file_duration(true);

  impl->encoder_factory_ = webrtc::CreateBuiltinVideoEncoderFactory();
  if (!impl->encoder_factory_) {
    LOG(ERROR) << "Failed to create webRTC built-in video encoder factory";
    return {};
  }

  return std::unique_ptr<LocalRecorder>(new LocalRecorder(std::move(impl)));
}

LocalRecorder::LocalRecorder(std::unique_ptr<LocalRecorder::Impl> impl)
    : impl_(std::move(impl)) {
}

LocalRecorder::~LocalRecorder() = default;

void LocalRecorder::AddDisplay(
    size_t width,
    size_t height,
    std::shared_ptr<webrtc::VideoTrackSourceInterface> source) {
  std::unique_ptr<Display> display(new Display(*impl_));
  display->source_ = source;

  std::lock_guard lock(impl_->mkv_mutex_);
  display->video_track_number_ =
      impl_->segment_.AddVideoTrack(width, height, 0);
  if (display->video_track_number_ == 0) {
    LOG(ERROR) << "Failed to add video track to webm muxer";
    return;
  }

  display->video_encoder_ =
      impl_->encoder_factory_->CreateVideoEncoder(webrtc::SdpVideoFormat("VP8"));
  if (!display->video_encoder_) {
    LOG(ERROR) << "Could not create vp8 video encoder";
    return;
  }
  auto rc =
      display->video_encoder_->RegisterEncodeCompleteCallback(display.get());
  if (rc != 0) {
    LOG(ERROR) << "Could not register encode complete callback";
    return;
  }
  source->AddOrUpdateSink(display.get(), rtc::VideoSinkWants{});

  webrtc::VideoCodec codec {};
  memset(&codec, 0, sizeof(codec));
  codec.codecType = webrtc::kVideoCodecVP8;
  codec.width = width;
  codec.height = height;
  codec.startBitrate = 1000; // kilobits/sec
  codec.maxBitrate = 2000;
  codec.minBitrate = 0;
  codec.maxFramerate = 60;
  codec.active = true;
  codec.qpMax = 56; // kDefaultMaxQp from simulcast_encoder_adapter.cc
  codec.mode = webrtc::VideoCodecMode::kScreensharing;
  codec.expect_encode_from_texture = false;
  *codec.VP8() = webrtc::VideoEncoder::GetDefaultVp8Settings();

  webrtc::VideoEncoder::Capabilities capabilities(false);
  webrtc::VideoEncoder::Settings settings(capabilities, 1, 1 << 20);

  rc = display->video_encoder_->InitEncode(&codec, settings);
  if (rc != 0) {
    LOG(ERROR) << "Failed to InitEncode";
    return;
  }

  display->encoder_running_ = true;
  display->encoder_thread_ = std::thread([](Display* display) {
    display->EncoderLoop();
  }, display.get());

  impl_->displays_.emplace_back(std::move(display));
}

void LocalRecorder::Stop() {
  for (auto& display : impl_->displays_) {
    display->Stop();
  }
  impl_->displays_.clear();

  std::lock_guard lock(impl_->mkv_mutex_);
  impl_->segment_.Finalize();
}

LocalRecorder::Display::Display(LocalRecorder::Impl& impl) : impl_(impl) {
}

void LocalRecorder::Display::OnFrame(const webrtc::VideoFrame& frame) {
  std::lock_guard queue_lock(encode_queue_mutex_);
  static int kMaxQueuedFrames = 10;
  if (encode_queue_.size() >= kMaxQueuedFrames) {
    LOG(VERBOSE) << "Dropped frame, encoder queue too long";
    return;
  }
  encode_queue_.push_back(frame);
  encoder_queue_signal_.notify_one();
}

void LocalRecorder::Display::EncoderLoop() {
  int frames_since_keyframe = 0;
  std::chrono::time_point<std::chrono::steady_clock> start_timestamp;
  auto last_keyframe_time = std::chrono::steady_clock::now();
  while (encoder_running_) {
    std::unique_ptr<webrtc::VideoFrame> frame;
    {
      std::unique_lock queue_lock(encode_queue_mutex_);
      while (encode_queue_.size() == 0 && encoder_running_) {
        encoder_queue_signal_.wait(queue_lock);
      }
      if (!encoder_running_) {
        break;
      }
      frame = std::make_unique<webrtc::VideoFrame>(
          std::move(encode_queue_.front()));
      encode_queue_.pop_front();
    }

    auto now = std::chrono::steady_clock::now();
    if (start_timestamp.time_since_epoch().count() == 0) {
      start_timestamp = now;
    }
    auto timestamp_diff =
        std::chrono::duration_cast<std::chrono::microseconds>(
              now - start_timestamp);
    frame->set_timestamp_us(timestamp_diff.count());
    frame->set_timestamp(timestamp_diff.count() * kRtpTicksPerUs);

    std::vector<webrtc::VideoFrameType> types;
    auto time_since_keyframe = now - last_keyframe_time;
    const auto min_keyframe_time = std::chrono::seconds(10);
    if (frames_since_keyframe > 60 || time_since_keyframe > min_keyframe_time) {
      last_keyframe_time = now;
      frames_since_keyframe = 0;
      types.push_back(webrtc::VideoFrameType::kVideoFrameKey);
    } else {
      types.push_back(webrtc::VideoFrameType::kVideoFrameDelta);
    }
    auto rc = video_encoder_->Encode(*frame, &types);
    if (rc != 0) {
      LOG(ERROR) << "Failed to encode frame";
    }
  }
}

void LocalRecorder::Display::Stop() {
  encoder_running_ = false;
  encoder_queue_signal_.notify_all();
  if (encoder_thread_.joinable()) {
    encoder_thread_.join();
  }
}

webrtc::EncodedImageCallback::Result LocalRecorder::Display::OnEncodedImage(
    const webrtc::EncodedImage& encoded_image,
    const webrtc::CodecSpecificInfo* codec_specific_info) {
  uint64_t timestamp = encoded_image.Timestamp() / kRtpTicksPerNs;

  std::lock_guard(impl_.mkv_mutex_);

  bool is_key =
      encoded_image._frameType == webrtc::VideoFrameType::kVideoFrameKey;
  bool success = impl_.segment_.AddFrame(
      encoded_image.data(),
      encoded_image.size(),
      video_track_number_,
      timestamp,
      is_key);

  webrtc::EncodedImageCallback::Result result(
      success
          ? webrtc::EncodedImageCallback::Result::Error::OK
          : webrtc::EncodedImageCallback::Result::Error::ERROR_SEND_FAILED);

  if (success) {
    result.frame_id = encoded_image.Timestamp();
  }
  return result;
}

} // namespace webrtc_streaming
} // namespace cuttlefish

