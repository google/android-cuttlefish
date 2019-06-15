#pragma once
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

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <hardware/gralloc.h>
#include <common/libs/fs/shared_fd.h>
#include "hwcomposer.h"

namespace cvd {

class FrameBuffer{
 public:
  FrameBuffer();
  ~FrameBuffer();

  void Broadcast(int32_t offset);
  int NextScreenBuffer();
  void* GetBuffer(int fb_index);
  size_t buffer_size();
  int32_t x_res();
  int32_t y_res();
  int32_t line_length();
  int32_t bytes_per_pixel();
  int32_t dpi();
  int32_t refresh_rate();
 private:
  bool ConnectToScreenServer();
  void GetScreenParameters();
  void BroadcastLoop();

  std::vector<char> inner_buffer_;
  int last_frame_buffer_ = 0;
  cvd::SharedFD screen_server_;
  std::thread broadcast_thread_;
  int32_t current_offset_ = 0;
  int32_t current_seq_ = 0;
  std::mutex mutex_;
  std::condition_variable cond_var_;
  bool running_ = true;
  int32_t x_res_{720};
  int32_t y_res_{1280};
  int32_t dpi_{160};
  int32_t refresh_rate_{60};
};

class BaseComposer {
 public:
  BaseComposer(int64_t vsync_base_timestamp);
  ~BaseComposer();

  // Sets the composition type of each layer and returns the number of layers
  // to be composited by the hwcomposer.
  int PrepareLayers(size_t num_layers, vsoc_hwc_layer* layers);
  // Returns 0 if successful.
  int SetLayers(size_t num_layers, vsoc_hwc_layer* layers);
  void Dump(char* buff, int buff_len);

  int32_t x_res() {
    return frame_buffer_.x_res();
  }
  int32_t y_res() {
    return frame_buffer_.y_res();
  }
  int32_t dpi() {
    return frame_buffer_.dpi();
  }
  int32_t refresh_rate() {
    return frame_buffer_.refresh_rate();
  }

 protected:
  const gralloc_module_t* gralloc_module_;
  int64_t vsync_base_timestamp_;
  int32_t vsync_period_ns_;
  FrameBuffer frame_buffer_;

 private:
  // Returns buffer offset or negative on error.
  int PostFrameBufferTarget(buffer_handle_t handle);
};

}  // namespace cvd
