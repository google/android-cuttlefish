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

#include "guest/hals/hwcomposer/vsocket_screen_view.h"

#include <algorithm>

#include <android-base/logging.h>
#include <cutils/properties.h>
#include <log/log.h>

#include "common/libs/device_config/device_config.h"
#include "common/libs/fs/shared_buf.h"

namespace cuttlefish {

VsocketScreenView::VsocketScreenView() {
  for (std::uint32_t i = 0; i < ScreenCount(); i++) {
    display_helpers_.emplace_back(new DisplayHelper(i));
  }

  broadcast_thread_ = std::thread([this]() { BroadcastLoop(); });
}

VsocketScreenView::~VsocketScreenView() {
  running_ = false;
  broadcast_thread_.join();
}

bool VsocketScreenView::ConnectToScreenServer() {
  auto vsock_frames_port = property_get_int64("ro.boot.vsock_frames_port", -1);
  if (vsock_frames_port <= 0) {
    ALOGI("No screen server configured, operating on headless mode");
    return false;
  }

  screen_server_ = cuttlefish::SharedFD::VsockClient(
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

  // The client detector thread needs to be started after the connection to the
  // socket has been made
  client_detector_thread_ = std::thread([this]() { ClientDetectorLoop(); });

  ALOGI("Broadcaster thread loop starting");
  while (true) {
    {
      std::unique_lock<std::mutex> lock(mutex_);

      while (running_) {
        if (std::any_of(display_helpers_.begin(),
                        display_helpers_.end(),
                        [](const auto& display_helper) {
                          return display_helper->HasPresentBuffer();
                        })) {
          break;
        }

        cond_var_.wait(lock);
      }
      if (!running_) {
        ALOGI("Broadcaster thread exiting");
        return;
      }
    }

    for (auto& display_helper : display_helpers_) {
      if (!display_helper->SendPresentBufferIfAvailable(&screen_server_)) {
        ALOGE("Broadcaster thread failed to send frame. Exiting...");
        return;
      }
    }
  }
}

void VsocketScreenView::ClientDetectorLoop() {
  char buffer[8];
  while (running_) {
    auto read = screen_server_->Read(buffer, sizeof(buffer));
    if (read == -1) {
      ALOGE("Client detector thread failed to read from screen server: %s",
            screen_server_->StrError());
      break;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // The last byte sent by the server indicates the presence of clients.
      send_frames_ = read > 0 && buffer[read - 1];
      cond_var_.notify_all();
    }
    if (read == 0) {
      ALOGE("screen server closed!");
      break;
    }
  }
}

std::uint8_t* VsocketScreenView::AcquireNextBuffer(
    std::uint32_t display_number) {
  CHECK_GE(display_helpers_.size(), display_number);
  return display_helpers_[display_number]->AcquireNextBuffer();
}

void VsocketScreenView::PresentAcquiredBuffer(std::uint32_t display_number) {
  CHECK_GE(display_helpers_.size(), display_number);
  display_helpers_[display_number]->PresentAcquiredBuffer();

  std::lock_guard<std::mutex> lock(mutex_);
  cond_var_.notify_all();
}

VsocketScreenView::DisplayHelper::DisplayHelper(
    std::uint32_t display_number) : display_number_(display_number) {
  buffer_size_ = ScreenSizeBytes(display_number);
  buffers_.resize(kNumBuffersPerDisplay * buffer_size_);

  for (std::uint32_t i = 0; i < kNumBuffersPerDisplay; i++) {
    acquirable_buffers_indexes_.push_back(i);
  }
}

std::uint8_t* VsocketScreenView::DisplayHelper::AcquireNextBuffer() {
  std::uint32_t acquired = 0;
  {
    std::lock_guard<std::mutex> lock(acquire_mutex_);

    CHECK(!acquirable_buffers_indexes_.empty());
    CHECK(!acquired_buffer_index_.has_value());

    acquired = acquirable_buffers_indexes_.front();
    acquirable_buffers_indexes_.pop_front();
    acquired_buffer_index_ = acquired;
  }

  return GetBuffer(acquired);
}

void VsocketScreenView::DisplayHelper::PresentAcquiredBuffer() {
  {
    std::lock_guard<std::mutex> present_lock(present_mutex_);
    {
      std::lock_guard<std::mutex> acquire_lock(acquire_mutex_);
      CHECK(acquired_buffer_index_.has_value());

      if (present_buffer_index_) {
        acquirable_buffers_indexes_.push_back(*present_buffer_index_);
      }

      present_buffer_index_ = *acquired_buffer_index_;
      acquired_buffer_index_.reset();
    }
  }
}

bool VsocketScreenView::DisplayHelper::HasPresentBuffer() {
  std::lock_guard<std::mutex> present_lock(present_mutex_);
  return present_buffer_index_.has_value();
}


bool VsocketScreenView::DisplayHelper::SendPresentBufferIfAvailable(
    cuttlefish::SharedFD* connection) {
  {
    std::lock_guard<std::mutex> present_lock(present_mutex_);

    if (present_buffer_index_) {
      std::uint32_t frame_buffer_index = *present_buffer_index_;
      std::uint8_t* frame_bytes = GetBuffer(frame_buffer_index);
      std::size_t frame_size_bytes = buffer_size_;

      if (WriteAllBinary(*connection, &display_number_) <= 0) {
        ALOGE("Failed to write: %s", strerror(errno));
        return false;
      }
      int32_t size = static_cast<int32_t>(frame_size_bytes);
      if (WriteAllBinary(*connection, &size) <= 0) {
        ALOGE("Failed to write: %s", strerror(errno));
        return false;
      }
      auto* raw = reinterpret_cast<const char*>(frame_bytes);
      if (WriteAll(*connection, raw, frame_size_bytes) <= 0) {
        ALOGE("Failed to write: %s", strerror(errno));
        return false;
      }

      {
        std::lock_guard<std::mutex> acquire_lock(acquire_mutex_);
        acquirable_buffers_indexes_.push_back(frame_buffer_index);
      }

      present_buffer_index_.reset();
    }

    return true;
  }
}

std::uint8_t* VsocketScreenView::DisplayHelper::GetBuffer(
    std::uint32_t buffer_index) {
  buffer_index %= kNumBuffersPerDisplay;
  return &buffers_[buffer_index * buffer_size_];
}

}  // namespace cuttlefish