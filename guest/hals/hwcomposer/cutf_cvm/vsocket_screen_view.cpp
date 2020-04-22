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

#define LOG_TAG "vsock_hwc"

#include "guest/hals/hwcomposer/cutf_cvm/vsocket_screen_view.h"

#include <cutils/properties.h>
#include <log/log.h>

#include "common/libs/device_config/device_config.h"

namespace cvd {

VsocketScreenView::VsocketScreenView()
    : broadcast_thread_([this]() { BroadcastLoop(); }) {
  GetScreenParameters();
  // inner_buffer needs to be initialized after the final values of the screen
  // parameters are set (either from the config server or default).
  inner_buffer_ = std::vector<char>(buffer_size() * 8);
}

VsocketScreenView::~VsocketScreenView() {
  running_ = false;
  broadcast_thread_.join();
}

void VsocketScreenView::GetScreenParameters() {
  auto device_config = cvd::DeviceConfig::Get();
  if (!device_config) {
    ALOGI(
        "Failed to obtain device configuration from server, running in "
        "headless mode");
    // It is impossible to ensure host and guest agree on the screen parameters
    // if these could not be read from the host configuration server. It's best
    // to not attempt to send frames in this case.
    running_ = false;
    // Do a phony Broadcast to ensure the broadcaster thread exits.
    Broadcast(-1);
    return;
  }
  x_res_ = device_config->screen_x_res();
  y_res_ = device_config->screen_y_res();
  dpi_ = device_config->screen_dpi();
  refresh_rate_ = device_config->screen_refresh_rate();
  ALOGI("Received screen parameters: res=%dx%d, dpi=%d, freq=%d", x_res_,
        y_res_, dpi_, refresh_rate_);
}

bool VsocketScreenView::ConnectToScreenServer() {
  auto vsock_frames_port = property_get_int64("ro.boot.vsock_frames_port", -1);
  if (vsock_frames_port <= 0) {
    ALOGI("No screen server configured, operating on headless mode");
    return false;
  }

  screen_server_ = cvd::SharedFD::VsockClient(
      2, static_cast<unsigned int>(vsock_frames_port), SOCK_STREAM);
  if (!screen_server_->IsOpen()) {
    ALOGE("Unable to connect to screen server: %s", screen_server_->StrError());
    return false;
  }

  return true;
}

void VsocketScreenView::BroadcastLoop() {
  auto connected = ConnectToScreenServer();
  if (!connected) {
    ALOGE(
        "Broadcaster thread exiting due to no connection to screen server. "
        "Compositions will occur, but frames won't be sent anywhere");
    return;
  }
  int current_seq = 0;
  int current_offset;
  ALOGI("Broadcaster thread loop starting");
  while (true) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      while (running_ && current_seq == current_seq_) {
        cond_var_.wait(lock);
      }
      if (!running_) {
        ALOGI("Broadcaster thread exiting");
        return;
      }
      current_offset = current_offset_;
      current_seq = current_seq_;
    }
    int32_t size = buffer_size();
    screen_server_->Write(&size, sizeof(size));
    auto buff = static_cast<char*>(GetBuffer(current_offset));
    while (size > 0) {
      auto written = screen_server_->Write(buff, size);
      if (written == -1) {
        ALOGE("Broadcaster thread failed to write frame: %s", strerror(errno));
        break;
      }
      size -= written;
      buff += written;
    }
  }
}

void VsocketScreenView::Broadcast(int offset, const CompositionStats*) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_offset_ = offset;
  current_seq_++;
  cond_var_.notify_all();
}

void* VsocketScreenView::GetBuffer(int buffer_id) {
  return &inner_buffer_[buffer_size() * buffer_id];
}

int32_t VsocketScreenView::x_res() const { return x_res_; }
int32_t VsocketScreenView::y_res() const { return y_res_; }
int32_t VsocketScreenView::dpi() const { return dpi_; }
int32_t VsocketScreenView::refresh_rate() const { return refresh_rate_; }

int VsocketScreenView::num_buffers() const {
  return inner_buffer_.size() / buffer_size();
}

}  // namespace cvd
