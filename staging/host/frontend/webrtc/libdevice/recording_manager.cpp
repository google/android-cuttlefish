/*
 * Copyright (C) 2023 The Android Open Source Project
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
#include "host/frontend/webrtc/libdevice/recording_manager.h"

#include <thread>
#include <vector>

#include <android-base/logging.h>
#include <rtc_base/time_utils.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "host/frontend/webrtc/libdevice/local_recorder.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace webrtc_streaming {

RecordingManager::RecordingManager() {
  auto cvd_config = cuttlefish::CuttlefishConfig::Get();
  CHECK(cvd_config);
  auto instance = cvd_config->ForDefaultInstance();

  instance_name_ = instance.instance_name();
  recording_directory_ = instance.PerInstancePath("recording/");
  recording_ = false;
}

void RecordingManager::AddSource(
    size_t width, size_t height,
    std::shared_ptr<webrtc::VideoTrackSourceInterface> video,
    const std::string& label) {
  LOG(VERBOSE) << "Display source is initiated in recording_manager. ";

  std::lock_guard<std::mutex> lock(mutex_);

  auto source = std::make_unique<Source>();
  source->width_ = width;
  source->height_ = height;
  source->video_ = video;
  sources_[label] = std::move(source);

  if (recording_) {
    StartSingleRecorder(label);
  }
  video_source_ready_signal_.notify_one();
}

void RecordingManager::RemoveSource(const std::string& label) {
  LOG(VERBOSE) << "Display source is removed in recording_manager. ";
  std::lock_guard<std::mutex> lock(mutex_);

  auto existing_recorder = local_recorders_.find(label);
  if (existing_recorder != local_recorders_.end()) {
    existing_recorder->second->Stop();
    local_recorders_.erase(existing_recorder);
  }

  sources_.erase(label);
  video_source_ready_signal_.notify_one();
}

void RecordingManager::Start() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (recording_) {
    LOG(VERBOSE) << "Video recording has been started! ";
    return;
  }

  for (auto& [label, source] : sources_) {
    StartSingleRecorder(label);
  }
  recording_ = true;
}

void RecordingManager::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!recording_) {
    LOG(VERBOSE) << "Video recording is not started, do noting in Stop.";
    return;
  }

  for (auto& [label, local_recorder] : local_recorders_) {
    local_recorder->Stop();
  }
  recording_ = false;

  local_recorders_.clear();
};

void RecordingManager::WaitForSources(size_t num_sources) {
  std::unique_lock<std::mutex> lock(mutex_);
  while (sources_.size() < num_sources) {
    video_source_ready_signal_.wait(lock);
  }
}

void RecordingManager::StartSingleRecorder(const std::string& label) {
  auto existing_source = sources_.find(label);
  if (existing_source == sources_.end()) {
    LOG(WARNING) << "Video recording is failed, no video source for: " << label;
    return;
  }

  int recording_time = rtc::TimeMillis();
  std::string recording_path = fmt::format("{}recording_{}_{}_{}.webm", recording_directory_,
                                           instance_name_, label, recording_time);
  std::unique_ptr<cuttlefish::webrtc_streaming::LocalRecorder> local_recorder =
      LocalRecorder::Create(recording_path);
  CHECK(local_recorder) << "Could not create local recorder";
  local_recorder->AddDisplay(label, existing_source->second->width_,
                             existing_source->second->height_, existing_source->second->video_);
  local_recorders_[label] = std::move(local_recorder);
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
