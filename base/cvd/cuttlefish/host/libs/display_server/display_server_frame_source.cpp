/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/display_server/display_server_frame_source.h"

#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/scoped_mmap.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/display_server/protocol.h"
#include "cuttlefish/host/libs/screen_connector/screen_connector_common.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish::display_server {

DisplayServerFrameSource::DisplayServerFrameSource(
    const std::string& socket_path) {
  for (int i = 0; i < 100 && !socket_->IsOpen(); ++i) {
    if (i > 0) {
      usleep(50000);
    }
    socket_ = SharedFD::SocketLocalClient(socket_path, false, SOCK_SEQPACKET);
  }
  CHECK(socket_->IsOpen()) << socket_->StrError();
  receive_thread_ = std::thread([this]() { ReceiveLoop(); });
}

DisplayServerFrameSource::~DisplayServerFrameSource() {
  running_ = false;
  if (socket_->IsOpen()) {
    socket_->Close();
  }
  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }
}

void DisplayServerFrameSource::SetFrameCallback(
    GenerateProcessedFrameCallbackImpl callback) {
  const bool ready = static_cast<bool>(callback);
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    frame_callback_ = std::move(callback);
  }
  if (ready) {
    // Frames are only useful once there is a callback to deliver them to.
    const ClientCommand command = ClientCommand::kReady;
    (void)socket_->Write(&command, sizeof(command));
  }
}

void DisplayServerFrameSource::SetDisplayEventCallback(
    DisplayEventCallback callback) {
  std::vector<DisplayCreatedEvent> existing_displays;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    display_event_callback_ = callback;
    if (callback) {
      for (const auto& [display_number, display] : displays_) {
        if (display.width > 0 && display.height > 0) {
          existing_displays.push_back(DisplayCreatedEvent{
              .display_number = display_number,
              .display_width = display.width,
              .display_height = display.height,
          });
        }
      }
    }
  }
  for (const DisplayCreatedEvent& event : existing_displays) {
    callback(event);
  }
}

bool DisplayServerFrameSource::AttachShm(uint32_t display_number,
                                         const DisplayMsg& msg) {
  if (msg.shm_name[0] == '\0' || msg.stride_bytes == 0 || msg.height == 0) {
    return false;
  }
  auto it = displays_.find(display_number);
  if (it != displays_.end() && it->second.shm &&
      it->second.shm_name == msg.shm_name) {
    return true;
  }
  if (it != displays_.end()) {
    displays_.erase(it);
  }

  SharedFD fd = SharedFD::ShmOpen(msg.shm_name, O_RDONLY, 0);
  size_t slot_size = static_cast<size_t>(msg.stride_bytes) * msg.height;
  ScopedMMap mapping =
      fd->IsOpen() ? fd->MMap(nullptr, slot_size * kFrameSlotsPerDisplay,
                              PROT_READ, MAP_SHARED, 0)
                   : ScopedMMap();
  if (!mapping) {
    return false;
  }

  displays_.emplace(display_number, MappedDisplay{
                                        .width = msg.width,
                                        .height = msg.height,
                                        .shm_name = msg.shm_name,
                                        .slot_size = slot_size,
                                        .shm = std::move(mapping),
                                    });
  return true;
}

void DisplayServerFrameSource::ReceiveLoop() {
  while (running_) {
    PollSharedFd pfd = {.fd = socket_,
                        // NOLINTNEXTLINE(misc-include-cleaner): <poll.h>
                        .events = POLLIN | POLLRDHUP | POLLHUP | POLLERR};
    if (SharedFD::Poll(&pfd, 1, 250) <= 0 || !running_) {
      continue;
    }
    if (pfd.revents & (POLLRDHUP | POLLHUP | POLLERR)) {
      break;
    }
    DisplayMsg msg = {};
    Result<uint64_t> bytes_read = socket_->Read(&msg, sizeof(msg));
    if (!bytes_read.has_value() || *bytes_read < sizeof(msg)) {
      continue;
    }
    msg.shm_name[sizeof(msg.shm_name) - 1] = '\0';
    HandleMessage(msg);
  }
}

void DisplayServerFrameSource::HandleMessage(const DisplayMsg& msg) {
  switch (msg.type) {
    case MsgType::kDisplayCreated:
      OnDisplayCreated(msg);
      return;
    case MsgType::kDisplayDestroyed:
      OnDisplayDestroyed(msg);
      return;
    case MsgType::kFrame:
      OnFrame(msg);
      return;
  }
  LOG(WARNING) << "Ignoring unknown display server message type "
               << static_cast<uint32_t>(msg.type);
}

void DisplayServerFrameSource::OnDisplayCreated(const DisplayMsg& msg) {
  DisplayEventCallback callback;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    MappedDisplay& display = displays_[msg.display_number];
    display.width = msg.width;
    display.height = msg.height;
    callback = display_event_callback_;
  }
  if (callback) {
    callback(DisplayCreatedEvent{
        .display_number = msg.display_number,
        .display_width = msg.width,
        .display_height = msg.height,
    });
  }
}

void DisplayServerFrameSource::OnDisplayDestroyed(const DisplayMsg& msg) {
  DisplayEventCallback callback;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    displays_.erase(msg.display_number);
    callback = display_event_callback_;
  }
  if (callback) {
    callback(DisplayDestroyedEvent{.display_number = msg.display_number});
  }
}

void DisplayServerFrameSource::OnFrame(const DisplayMsg& msg) {
  if (msg.slot >= kFrameSlotsPerDisplay) {
    return;
  }
  GenerateProcessedFrameCallbackImpl callback;
  uint8_t* pixels = nullptr;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (!AttachShm(msg.display_number, msg) || !frame_callback_) {
      return;
    }
    MappedDisplay& display = displays_[msg.display_number];
    pixels =
        static_cast<uint8_t*>(display.shm.get()) + msg.slot * display.slot_size;
    callback = frame_callback_;
  }
  callback(msg.display_number, msg.width, msg.height, msg.fourcc_format,
           msg.stride_bytes, pixels);
}

}  // namespace cuttlefish::display_server
