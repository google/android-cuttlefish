/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include <android/hardware/graphics/mapper/2.0/IMapper.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include "utils/Timers.h"
#include "vsock_connection.h"

namespace cuttlefish {

using ::android::hardware::graphics::mapper::V2_0::YCbCrLayout;

// VsockFrameProvider reads data from vsock
// Users can get the data by using copyYUVFrame/copyJpegData methods
class VsockFrameProvider {
 public:
  VsockFrameProvider() = default;
  ~VsockFrameProvider();

  VsockFrameProvider(const VsockFrameProvider&) = delete;
  VsockFrameProvider& operator=(const VsockFrameProvider&) = delete;

  void start(std::shared_ptr<cuttlefish::VsockConnection> connection,
             uint32_t expected_width, uint32_t expected_height);
  void stop();
  void requestJpeg();
  void cancelJpegRequest();
  bool jpegPending() const { return jpeg_pending_.load(); }
  bool isRunning() const { return running_.load(); }
  bool waitYUVFrame(unsigned int max_wait_ms);
  bool copyYUVFrame(uint32_t width, uint32_t height, YCbCrLayout dst);
  bool copyJpegData(uint32_t size, void* dst);

 private:
  bool isBlob(const std::vector<char>& blob);
  bool framesizeMatches(uint32_t width, uint32_t height,
                        const std::vector<char>& data);
  void VsockReadLoop(uint32_t expected_width, uint32_t expected_height);
  std::thread reader_thread_;
  std::mutex frame_mutex_;
  std::mutex jpeg_mutex_;
  std::atomic<nsecs_t> timestamp_;
  std::atomic<bool> running_;
  std::atomic<bool> jpeg_pending_;
  std::vector<char> frame_;
  std::vector<char> next_frame_;
  std::vector<char> cached_jpeg_;
  std::condition_variable yuv_frame_updated_;
  std::shared_ptr<cuttlefish::VsockConnection> connection_;
};

}  // namespace cuttlefish
