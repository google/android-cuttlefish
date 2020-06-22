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
#include "guest/hals/hwcomposer/common/screen_view.h"

namespace cuttlefish {

class VsocketScreenView : public ScreenView {
 public:
  VsocketScreenView();
  virtual ~VsocketScreenView();

  void Broadcast(int buffer_id,
                 const CompositionStats* stats = nullptr) override;
  void* GetBuffer(int fb_index) override;

  int32_t x_res() const override;
  int32_t y_res() const override;
  int32_t dpi() const override;
  int32_t refresh_rate() const override;

  int num_buffers() const override;

 private:
  bool ConnectToScreenServer();
  void GetScreenParameters();
  void BroadcastLoop();

  std::vector<char> inner_buffer_;
  cuttlefish::SharedFD screen_server_;
  std::thread broadcast_thread_;
  int current_offset_ = 0;
  int current_seq_ = 0;
  std::mutex mutex_;
  std::condition_variable cond_var_;
  bool running_ = true;
  int32_t x_res_{720};
  int32_t y_res_{1280};
  int32_t dpi_{160};
  int32_t refresh_rate_{60};
};

}  // namespace cuttlefish
