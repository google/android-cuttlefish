/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "host/libs/screen_connector/screen_connector.h"

#include <atomic>
#include <cinttypes>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace cvd {

class SocketBasedScreenConnector : public ScreenConnector {
 public:
  explicit SocketBasedScreenConnector(int frames_fd);

  bool OnFrameAfter(std::uint32_t frame_number,
                    const FrameCallback& frame_callback) override;

 private:
  static constexpr int NUM_BUFFERS_ = 4;

  int WaitForNewFrameSince(std::uint32_t* seq_num);
  void* GetBuffer(int buffer_idx);
  void ServerLoop(int frames_fd);
  void BroadcastNewFrame(int buffer_idx);

  std::vector<std::uint8_t> buffer_ =
      std::vector<std::uint8_t>(NUM_BUFFERS_ * ScreenSizeInBytes());
  std::uint32_t seq_num_{0};
  int newest_buffer_ = 0;
  std::condition_variable new_frame_cond_var_;
  std::mutex new_frame_mtx_;
  std::thread screen_server_thread_;
};

} // namespace cvd