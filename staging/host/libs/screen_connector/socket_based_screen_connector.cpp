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

#include "host/libs/screen_connector/screen_connector_common.h"
#include "host/libs/screen_connector/socket_based_screen_connector.h"

#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"

namespace cuttlefish {

SocketBasedScreenConnector::SocketBasedScreenConnector(int frames_fd) {
  for (std::uint32_t i = 0; i < ScreenConnectorInfo::ScreenCount(); i++) {
    display_helpers_.emplace_back(new DisplayHelper(i));
  }

  screen_server_thread_ =
    std::thread([this, frames_fd]() { ServerLoop(frames_fd); });
}

SocketBasedScreenConnector::DisplayHelper::DisplayHelper(
    std::uint32_t display_number)
    : display_number_(display_number) {
  buffer_size_ = ScreenConnectorInfo::ScreenSizeInBytes(display_number);
  buffers_.resize(kNumBuffersPerDisplay * buffer_size_);

  for (std::uint32_t i = 0; i < kNumBuffersPerDisplay; i++) {
    acquirable_buffers_indexes_.push_back(i);
  }
}

std::uint8_t* SocketBasedScreenConnector::DisplayHelper::AcquireNextBuffer() {
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

void SocketBasedScreenConnector::DisplayHelper::PresentAcquiredBuffer() {
  std::lock_guard<std::mutex> acquire_lock(acquire_mutex_);
  CHECK(acquired_buffer_index_.has_value());

  std::uint32_t prexisting_present_buffer_index =
      present_buffer_index_.exchange(*acquired_buffer_index_);

  if (prexisting_present_buffer_index != kInvalidBufferIndex) {
    acquirable_buffers_indexes_.push_back(prexisting_present_buffer_index);
  }

  acquired_buffer_index_.reset();
}

bool SocketBasedScreenConnector::DisplayHelper::ConsumePresentBuffer(
    const GenerateProcessedFrameCallbackImpl& frame_callback) {
  std::uint32_t present_buffer_index =
      present_buffer_index_.exchange(kInvalidBufferIndex);

  if (present_buffer_index != kInvalidBufferIndex) {
    std::uint8_t* present_buffer_bytes = GetBuffer(present_buffer_index);

    frame_callback(display_number_, present_buffer_bytes);

    {
      std::lock_guard<std::mutex> acquire_lock(acquire_mutex_);
      acquirable_buffers_indexes_.push_back(present_buffer_index);
    }

    return true;
  } else {
    return false;
  }
}

std::uint8_t* SocketBasedScreenConnector::DisplayHelper::GetBuffer(
    std::uint32_t buffer_index) {
  return &buffers_[buffer_index * buffer_size_];
}

bool SocketBasedScreenConnector::OnNextFrame(
    const GenerateProcessedFrameCallbackImpl& frame_callback) {
  while (true) {
    std::unique_lock<std::mutex> lock(frame_available_mutex_);

    for (std::size_t i = 0; i < display_helpers_.size(); i++) {
      auto& display_helper = display_helpers_[frame_available_display_index_];
      if (display_helper->ConsumePresentBuffer(frame_callback)) {
        return true;
      }

      frame_available_display_index_ =
          (frame_available_display_index_ + 1) % display_helpers_.size();
    }

    frame_available_cond_var_.wait(lock);
  }

  return false;
}

void SocketBasedScreenConnector::ServerLoop(int frames_fd) {
  if (frames_fd < 0) {
    LOG(FATAL) << "Invalid file descriptor: " << frames_fd;
    return;
  }
  auto server = SharedFD::Dup(frames_fd);
  close(frames_fd);
  if (!server->IsOpen()) {
    LOG(FATAL) << "Unable to dup screen server: " << server->StrError();
    return;
  }

  while (1) {
    LOG(DEBUG) << "Screen Connector accepting connections...";
    client_connection_ = SharedFD::Accept(*server);
    if (!client_connection_->IsOpen()) {
      LOG(ERROR) << "Disconnected fd returned from accept";
      continue;
    }
    ReportClientsConnected(have_clients_);
    while (client_connection_->IsOpen()) {
      uint32_t display_number = 0;
      if (client_connection_->Read(&display_number, sizeof(display_number)) <
          0) {
        LOG(ERROR) << "Failed to read from hwcomposer: "
                   << client_connection_->StrError();
        break;
      }

      int32_t size = 0;
      if (client_connection_->Read(&size, sizeof(size)) < 0) {
        LOG(ERROR) << "Failed to read from hwcomposer: " << client_connection_->StrError();
        break;
      }

      auto& display_helper = display_helpers_[display_number];

      std::uint8_t* buff = display_helper->AcquireNextBuffer();
      while (size > 0) {
        auto read = client_connection_->Read(buff, size);
        if (read < 0) {
          LOG(ERROR) << "Failed to read from hwcomposer: " << client_connection_->StrError();
          client_connection_->Close();
          break;
        }
        size -= read;
        buff += read;
      }

      display_helper->PresentAcquiredBuffer();
      frame_available_cond_var_.notify_all();
    }
  }
}

void SocketBasedScreenConnector::ReportClientsConnected(bool have_clients) {
  have_clients_ = have_clients;
  char buffer = have_clients ? 1 : 0;
  (void)client_connection_->Write(&buffer, sizeof(buffer));
}

} // namespace cuttlefish
