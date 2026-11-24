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

#include "cuttlefish/host/frontend/webrtc/libdevice/local_recorder.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "api/environment/environment_factory.h"
#include "api/media_stream_interface.h"
#include "api/rtp_parameters.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/video_encoder.h"
#include "mkvmuxer/mkvmuxer.h"
#include "mkvmuxer/mkvwriter.h"
#include "system_wrappers/include/clock.h"

namespace cuttlefish {
namespace webrtc_streaming {

constexpr double kRtpTicksPerSecond = 90000.;
constexpr double kRtpTicksPerMs = kRtpTicksPerSecond / 1000.;
constexpr double kRtpTicksPerUs = kRtpTicksPerMs / 1000.;
constexpr double kRtpTicksPerNs = kRtpTicksPerUs / 1000.;

class LocalRecorder::Display
    : public webrtc::EncodedImageCallback,
      public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  Display(LocalRecorder::Impl& impl);
  ~Display() {
    CHECK(!encoder_running_)
        << "LocalRecorder::Display destroyed before calling Stop()";
  }

  std::unique_ptr<webrtc::VideoEncoder> InitializeEncoder(size_t width,
                                                          size_t height);
  void EncoderLoop(std::unique_ptr<webrtc::VideoEncoder> video_encoder);
  void Stop();

  // VideoSinkInterface
  virtual void OnFrame(const webrtc::VideoFrame& frame) override;

  // EncodedImageCallback
  virtual webrtc::EncodedImageCallback::Result OnEncodedImage(
      const webrtc::EncodedImage& encoded_image,
      const webrtc::CodecSpecificInfo* codec_specific_info) override;
  void OnFrameDropped(uint32_t rtp_timestamp, int spatial_id,
                      bool is_end_of_temporal_unit) override {}

  LocalRecorder::Impl& impl_;
  std::shared_ptr<webrtc::VideoTrackSourceInterface> source_;
  uint64_t video_track_number_ = 0;

  // TODO(schuffelen): Use a WebRTC task queue?
  std::thread encoder_thread_;
  std::condition_variable encoder_queue_signal_;
  std::mutex encode_queue_mutex_;
  std::list<webrtc::VideoFrame> encode_queue_;
  std::atomic_bool encoder_running_ = false;
};

class LocalRecorder::Impl {
 public:
  mkvmuxer::MkvWriter file_writer_;
  mkvmuxer::Segment segment_;
  std::unique_ptr<webrtc::VideoEncoderFactory> encoder_factory_;
  std::mutex mkv_mutex_;
  std::map<std::string, std::unique_ptr<Display>> displays_;
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
    : impl_(std::move(impl)) {}

LocalRecorder::~LocalRecorder() = default;

void LocalRecorder::AddDisplay(
    const std::string& label, size_t width, size_t height,
    std::shared_ptr<webrtc::VideoTrackSourceInterface> source) {
  LOG(INFO) << "Display added with label '" << label << "'";

  std::lock_guard lock(impl_->mkv_mutex_);

  const auto existing_display = impl_->displays_.find(label);
  if (existing_display != impl_->displays_.end()) {
    Display* const display = existing_display->second.get();
    CHECK(display);
    if (display->source_ && display->source_ != source) {
      display->source_->RemoveSink(display);
    }
    display->source_ = source;
    source->AddOrUpdateSink(display, webrtc::VideoSinkWants{});
    return;
  }

  std::unique_ptr<Display> display(new Display(*impl_));
  display->source_ = source;
  display->video_track_number_ =
      impl_->segment_.AddVideoTrack(width, height, 0);
  if (display->video_track_number_ == 0) {
    LOG(ERROR) << "Failed to add video track to webm muxer";
    return;
  }

  std::promise<bool> init_promise;
  std::future<bool> init_future = init_promise.get_future();

  display->encoder_thread_ =
      std::thread([display_ptr = display.get(), width, height,
                   promise = std::move(init_promise)]() mutable {
        std::unique_ptr<webrtc::VideoEncoder> video_encoder =
            display_ptr->InitializeEncoder(width, height);
        const bool initialized = video_encoder != nullptr;
        if (initialized) {
          display_ptr->encoder_running_ = true;
        }
        promise.set_value(initialized);
        if (initialized) {
          display_ptr->EncoderLoop(std::move(video_encoder));
        }
      });

  if (!init_future.get()) {
    if (display->encoder_thread_.joinable()) {
      display->encoder_thread_.join();
    }
    return;
  }

  source->AddOrUpdateSink(display.get(), webrtc::VideoSinkWants{});
  impl_->displays_[label] = std::move(display);
}

void LocalRecorder::Stop() {
  for (auto& [label, display] : impl_->displays_) {
    display->Stop();
  }
  std::lock_guard lock(impl_->mkv_mutex_);
  impl_->segment_.Finalize();
}

LocalRecorder::Display::Display(LocalRecorder::Impl& impl) : impl_(impl) {}

void LocalRecorder::Display::OnFrame(const webrtc::VideoFrame& frame) {
  std::lock_guard queue_lock(encode_queue_mutex_);
  static int kMaxQueuedFrames = 10;
  if (encode_queue_.size() >= kMaxQueuedFrames) {
    VLOG(1) << "Dropped frame, encoder queue too long";
    return;
  }
  encode_queue_.push_back(frame);
  encoder_queue_signal_.notify_one();
}

std::unique_ptr<webrtc::VideoEncoder> LocalRecorder::Display::InitializeEncoder(
    size_t width, size_t height) {
  std::unique_ptr<webrtc::VideoEncoder> video_encoder =
      impl_.encoder_factory_->Create(webrtc::CreateEnvironment(),
                                     webrtc::SdpVideoFormat("VP8"));
  if (!video_encoder) {
    LOG(ERROR) << "Could not create vp8 video encoder";
    return nullptr;
  }
  const int register_rc = video_encoder->RegisterEncodeCompleteCallback(this);
  if (register_rc != 0) {
    LOG(ERROR) << "Could not register encode complete callback";
    return nullptr;
  }

  webrtc::VideoCodec codec{};
  codec.codecType = webrtc::kVideoCodecVP8;
  codec.width = width;
  codec.height = height;
  codec.startBitrate = 1000;  // kilobits/sec
  codec.maxBitrate = 2000;
  codec.minBitrate = 0;
  codec.maxFramerate = 60;
  codec.active = true;
  codec.qpMax = 56;  // kDefaultMaxQp from simulcast_encoder_adapter.cc
  codec.mode = webrtc::VideoCodecMode::kScreensharing;
  codec.expect_encode_from_texture = false;
  *codec.VP8() = webrtc::VideoEncoder::GetDefaultVp8Settings();

  const webrtc::VideoEncoder::Capabilities capabilities(false);
  const webrtc::VideoEncoder::Settings settings(capabilities, 1, 1 << 20);

  const int init_rc = video_encoder->InitEncode(&codec, settings);
  if (init_rc != 0) {
    LOG(ERROR) << "Failed to InitEncode";
    video_encoder->RegisterEncodeCompleteCallback(nullptr);
    return nullptr;
  }

  return video_encoder;
}

void LocalRecorder::Display::EncoderLoop(
    std::unique_ptr<webrtc::VideoEncoder> video_encoder) {
  int frames_since_keyframe = 0;
  std::chrono::time_point<std::chrono::steady_clock> start_timestamp;
  std::chrono::time_point<std::chrono::steady_clock> last_keyframe_time =
      std::chrono::steady_clock::now();
  while (encoder_running_) {
    std::unique_ptr<webrtc::VideoFrame> frame;
    {
      std::unique_lock queue_lock(encode_queue_mutex_);
      while (encode_queue_.empty() && encoder_running_) {
        encoder_queue_signal_.wait(queue_lock);
      }
      if (!encoder_running_) {
        break;
      }
      frame = std::make_unique<webrtc::VideoFrame>(
          std::move(encode_queue_.front()));
      encode_queue_.pop_front();
    }

    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    if (start_timestamp.time_since_epoch().count() == 0) {
      start_timestamp = now;
    }
    const std::chrono::microseconds timestamp_diff =
        std::chrono::duration_cast<std::chrono::microseconds>(now -
                                                              start_timestamp);
    frame->set_timestamp_us(timestamp_diff.count());
    frame->set_rtp_timestamp(timestamp_diff.count() * kRtpTicksPerUs);

    std::vector<webrtc::VideoFrameType> types;
    const std::chrono::steady_clock::duration time_since_keyframe =
        now - last_keyframe_time;
    const std::chrono::seconds min_keyframe_time(10);
    if (frames_since_keyframe > 60 || time_since_keyframe > min_keyframe_time) {
      last_keyframe_time = now;
      frames_since_keyframe = 0;
      types.push_back(webrtc::VideoFrameType::kVideoFrameKey);
    } else {
      types.push_back(webrtc::VideoFrameType::kVideoFrameDelta);
    }
    const int encode_rc = video_encoder->Encode(*frame, &types);
    if (encode_rc != 0) {
      LOG(ERROR) << "Failed to encode frame";
    }
  }

  video_encoder->RegisterEncodeCompleteCallback(nullptr);
  video_encoder->Release();
}

void LocalRecorder::Display::Stop() {
  source_->RemoveSink(this);
  encoder_running_ = false;
  encoder_queue_signal_.notify_all();
  if (encoder_thread_.joinable()) {
    encoder_thread_.join();
  }
}

webrtc::EncodedImageCallback::Result LocalRecorder::Display::OnEncodedImage(
    const webrtc::EncodedImage& encoded_image,
    const webrtc::CodecSpecificInfo* codec_specific_info) {
  uint64_t timestamp = encoded_image.RtpTimestamp() / kRtpTicksPerNs;

  std::lock_guard lock(impl_.mkv_mutex_);

  bool is_key =
      encoded_image._frameType == webrtc::VideoFrameType::kVideoFrameKey;
  bool success =
      impl_.segment_.AddFrame(encoded_image.data(), encoded_image.size(),
                              video_track_number_, timestamp, is_key);

  webrtc::EncodedImageCallback::Result result(
      success ? webrtc::EncodedImageCallback::Result::Error::OK
              : webrtc::EncodedImageCallback::Result::Error::ERROR_SEND_FAILED);

  if (success) {
    result.frame_id = encoded_image.RtpTimestamp();
  }
  return result;
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
