/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "guest/hals/hwcomposer/screen_view.h"

namespace cuttlefish {

class VsocketScreenView : public ScreenView {
 public:
  VsocketScreenView();
  virtual ~VsocketScreenView();

  void Broadcast(int buffer_id,
                 const CompositionStats* stats = nullptr) override;
  void* GetBuffer(int fb_index) override;

  int num_buffers() const override;

 private:
  bool ConnectToScreenServer();
  void GetScreenParameters();
  void BroadcastLoop();
  void ClientDetectorLoop();
  bool SendFrame(int offset);

  std::uint32_t inner_buffer_size_;
  std::vector<char> inner_buffer_;
  cuttlefish::SharedFD screen_server_;
  std::thread broadcast_thread_;
  std::thread client_detector_thread_;
  int current_offset_ = 0;
  unsigned int current_seq_ = 0;
  std::mutex mutex_;
  std::condition_variable cond_var_;
  bool running_ = true;
  bool send_frames_{false};
};

}  // namespace cuttlefish
