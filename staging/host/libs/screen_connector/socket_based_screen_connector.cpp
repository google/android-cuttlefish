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
  screen_server_thread_ =
    std::thread([this, frames_fd]() { ServerLoop(frames_fd); });

  buffer_size_ = ScreenConnectorInfo::ScreenSizeInBytes(/*display_number=*/0);
  buffer_.resize(kNumBuffersPerDisplay * buffer_size_);
}

bool SocketBasedScreenConnector::OnFrameAfter(std::uint32_t frame_number,
                                              const GenerateProcessedFrameCallbackImpl& frame_callback) {
  int buffer_idx = WaitForNewFrameSince(&frame_number);
  void* buffer = GetBuffer(buffer_idx);
  frame_callback(frame_number, reinterpret_cast<uint8_t*>(buffer));
  return true;
}

int SocketBasedScreenConnector::WaitForNewFrameSince(std::uint32_t* seq_num) {
  std::unique_lock<std::mutex> lock(new_frame_mtx_);
  while (seq_num_ == *seq_num) {
    new_frame_cond_var_.wait(lock);
  }
  *seq_num = seq_num_;
  return newest_buffer_;
}

void* SocketBasedScreenConnector::GetBuffer(int buffer_idx) {
  if (buffer_idx < 0) return nullptr;
  buffer_idx %= kNumBuffersPerDisplay;
  return &buffer_[buffer_idx * buffer_size_];
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

  int current_buffer = 0;

  while (1) {
    LOG(DEBUG) << "Screen Connector accepting connections...";
    client_connection_ = SharedFD::Accept(*server);
    if (!client_connection_->IsOpen()) {
      LOG(ERROR) << "Disconnected fd returned from accept";
      continue;
    }
    ReportClientsConnected(have_clients_);
    while (client_connection_->IsOpen()) {
      int32_t size = 0;
      if (client_connection_->Read(&size, sizeof(size)) < 0) {
        LOG(ERROR) << "Failed to read from hwcomposer: " << client_connection_->StrError();
        break;
      }
      auto buff = reinterpret_cast<uint8_t*>(GetBuffer(current_buffer));
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
      BroadcastNewFrame(current_buffer);
      current_buffer = (current_buffer + 1) % kNumBuffersPerDisplay;
    }
  }
}

void SocketBasedScreenConnector::ReportClientsConnected(bool have_clients) {
  have_clients_ = have_clients;
  char buffer = have_clients ? 1 : 0;
  (void)client_connection_->Write(&buffer, sizeof(buffer));
}

void SocketBasedScreenConnector::BroadcastNewFrame(int buffer_idx) {
  {
    std::lock_guard<std::mutex> lock(new_frame_mtx_);
    seq_num_++;
    newest_buffer_ = buffer_idx;
  }
  new_frame_cond_var_.notify_all();
}
} // namespace cuttlefish
