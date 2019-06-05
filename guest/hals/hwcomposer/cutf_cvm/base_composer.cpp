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

#include "base_composer.h"

#include <string.h>

#include <cutils/properties.h>
#include <hardware/gralloc.h>
#include <log/log.h>

#include <common/libs/utils/size_utils.h>
#include "guest/hals/gralloc/legacy/gralloc_vsoc_priv.h"

namespace cvd {

BaseComposer::BaseComposer(int64_t vsync_base_timestamp)
    : vsync_base_timestamp_(vsync_base_timestamp) {
  vsync_period_ns_ = 1000000000 / frame_buffer_.refresh_rate();
  hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                reinterpret_cast<const hw_module_t**>(&gralloc_module_));
}

BaseComposer::~BaseComposer() {}

void BaseComposer::Dump(char* buff __unused, int buff_len __unused) {}

int BaseComposer::PostFrameBufferTarget(buffer_handle_t buffer_handle) {
  int fb_index = frame_buffer_.NextScreenBuffer();
  void* frame_buffer = frame_buffer_.GetBuffer(fb_index);
  const private_handle_t* p_handle =
      reinterpret_cast<const private_handle_t*>(buffer_handle);
  void* buffer;
  int retval = gralloc_module_->lock(gralloc_module_, buffer_handle,
                                     GRALLOC_USAGE_SW_READ_OFTEN, 0, 0,
                                     p_handle->x_res, p_handle->y_res, &buffer);
  if (retval != 0) {
    ALOGE("Got error code %d from lock function", retval);
    return -1;
  }
  memcpy(frame_buffer, buffer, frame_buffer_.buffer_size());
  frame_buffer_.Broadcast(fb_index);
  return 0;
}  // namespace cvd

int BaseComposer::PrepareLayers(size_t num_layers, vsoc_hwc_layer* layers) {
  // find unsupported overlays
  for (size_t i = 0; i < num_layers; i++) {
    if (IS_TARGET_FRAMEBUFFER(layers[i].compositionType)) {
      continue;
    }
    layers[i].compositionType = HWC_FRAMEBUFFER;
  }
  return 0;
}

int BaseComposer::SetLayers(size_t num_layers, vsoc_hwc_layer* layers) {
  for (size_t idx = 0; idx < num_layers; idx++) {
    if (IS_TARGET_FRAMEBUFFER(layers[idx].compositionType)) {
      return PostFrameBufferTarget(layers[idx].handle);
    }
  }
  return -1;
}

FrameBuffer::FrameBuffer()
    : broadcast_thread_([this]() { BroadcastLoop(); }) {
  auto vsock_frames_port = property_get_int32("ro.boot.vsock_frames_port", -1);
  if (vsock_frames_port > 0) {
    screen_server_ = cvd::SharedFD::VsockClient(2, vsock_frames_port,
                                                SOCK_STREAM);
    if (screen_server_->IsOpen()) {
      // TODO(b/128842613): Get this info from the configuration server
      int32_t screen_params[4];
      auto res = screen_server_->Read(screen_params, sizeof(screen_params));
      if (res == sizeof(screen_params)) {
        x_res_ = screen_params[0];
        y_res_ = screen_params[1];
        dpi_ = screen_params[2];
        refresh_rate_ = screen_params[3];
      } else {
        LOG(ERROR) << "Unable to get screen configuration parameters from screen "
                   << "server (" << res << "): " << screen_server_->StrError();
      }
    } else {
      LOG(ERROR) << "Unable to connect to screen server: "
                 << screen_server_->StrError();
    }
  } else {
    LOG(INFO) << "No screen server configured, operating on headless mode";
  }
  // This needs to happen no matter what, otherwise there won't be a buffer for
  // the set calls to compose on.
  inner_buffer_ = std::vector<char>(buffer_size() * 8);
}

FrameBuffer::~FrameBuffer() {
  running_ = false;
  broadcast_thread_.join();
}

int FrameBuffer::NextScreenBuffer() {
  int num_buffers = inner_buffer_.size() / buffer_size();
  last_frame_buffer_ =
      num_buffers > 0 ? (last_frame_buffer_ + 1) % num_buffers : -1;
  return last_frame_buffer_;
}

void FrameBuffer::BroadcastLoop() {
  if (!screen_server_->IsOpen()) {
    LOG(ERROR) << "Broadcaster thread exiting due to no connection to screen"
               << " server. Compositions will occur, but frames won't be sent"
               << " anywhere";
    return;
  }
  int32_t current_seq = 0;
  int32_t current_offset;
  while (running_) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      while (current_seq == current_seq_) {
        cond_var_.wait(lock);
      }
      current_offset = current_offset_;
      current_seq = current_seq_;
    }
    int32_t size = buffer_size();
    screen_server_->Write(&size, sizeof(size));
    auto buff = static_cast<char*>(GetBuffer(current_offset));
    while (size > 0) {
      auto written = screen_server_->Write(buff, size);
      size -= written;
      buff += written;
    }
  }
}

void FrameBuffer::Broadcast(int32_t offset) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_offset_ = offset;
  current_seq_++;
  cond_var_.notify_all();
}
void* FrameBuffer::GetBuffer(int fb_index) {
  return &inner_buffer_[buffer_size() * fb_index];
}
size_t FrameBuffer::buffer_size() {
  return (line_length() * y_res()) + 4;
}
int32_t FrameBuffer::x_res() { return x_res_; }
int32_t FrameBuffer::y_res() { return y_res_; }
int32_t FrameBuffer::line_length() {
  return cvd::AlignToPowerOf2(x_res() * bytes_per_pixel(), 4);
}
int32_t FrameBuffer::bytes_per_pixel() { return 4; }
int32_t FrameBuffer::dpi() { return dpi_; }
int32_t FrameBuffer::refresh_rate() { return refresh_rate_; }

}  // namespace cvd
