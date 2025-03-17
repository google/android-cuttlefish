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

#pragma once

#include "host/frontend/webrtc/libdevice/local_recorder.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/libs/fs/shared_fd.h"

namespace webrtc {
class VideoTrackSourceInterface;
}

namespace cuttlefish {
namespace webrtc_streaming {

class Source {
public:
  size_t width_;
  size_t height_;
  std::shared_ptr<webrtc::VideoTrackSourceInterface> video_;
};

class RecordingManager {
public:
  RecordingManager();

  void AddSource(size_t width, size_t height,
           std::shared_ptr<webrtc::VideoTrackSourceInterface> video,
           const std::string& label);
  void RemoveSource(const std::string& label);
  void Start();
  void Stop();
  void WaitForSources(size_t num_sources);

private:
  bool recording_;
  std::string recording_directory_;
  std::string instance_name_;
  std::mutex mutex_;
  std::condition_variable video_source_ready_signal_;
  std::map<std::string, std::unique_ptr<Source>> sources_;
  std::map<std::string,
           std::unique_ptr<cuttlefish::webrtc_streaming::LocalRecorder>> local_recorders_;

  void StartSingleRecorder(const std::string& label);
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish