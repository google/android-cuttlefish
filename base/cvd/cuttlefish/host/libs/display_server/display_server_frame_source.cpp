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
#include <sys/eventfd.h>  // IWYU pragma: keep: EFD_CLOEXEC
#include <sys/mman.h>
#include <sys/socket.h>  // IWYU pragma: keep: SOCK_SEQPACKET
#include <sys/stat.h>    // IWYU pragma: keep: struct stat

#include <array>
#include <cerrno>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/scoped_mmap.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/display_server/protocol.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish::display_server {

Result<std::unique_ptr<DisplayServerFrameSource>>
DisplayServerFrameSource::Create(const std::string& socket_path) {
  // run_cvd creates the listening socket before starting this process, so a
  // single connection attempt is enough.
  SharedFD socket =
      SharedFD::SocketLocalClient(socket_path, false, SOCK_SEQPACKET);
  CF_EXPECTF(socket->IsOpen(), "Failed to connect to '{}': {}", socket_path,
             socket->StrError());
  SharedFD stop_event =
      CF_EXPECT(Fd::Event(0, EFD_CLOEXEC), "Failed to create stop eventfd");
  return std::unique_ptr<DisplayServerFrameSource>(
      new DisplayServerFrameSource(std::move(socket), std::move(stop_event)));
}

DisplayServerFrameSource::DisplayServerFrameSource(SharedFD socket,
                                                   SharedFD stop_event)
    : socket_(std::move(socket)), stop_event_(std::move(stop_event)) {
  receive_thread_ = std::thread([this]() { ReceiveLoop(); });
}

DisplayServerFrameSource::~DisplayServerFrameSource() {
  if (stop_event_->EventfdWrite(1) != 0) {
    LOG(ERROR) << "Failed to signal the receive thread to stop: "
               << stop_event_->StrError();
  }
  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }
}

void DisplayServerFrameSource::SetFrameCallback(FrameCallback callback) {
  std::lock_guard<std::mutex> delivery_lock(frame_delivery_mutex_);
  std::vector<std::pair<uint32_t, Frame>> latest_frames;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    frame_callback_ = callback;
    if (callback) {
      for (auto& [display_number, display] : displays_) {
        if (!display.last_slot) {
          continue;
        }
        Result<Frame> frame = GetFrameFromShm(display, *display.last_slot);
        if (frame.has_value()) {
          latest_frames.emplace_back(display_number, *frame);
        }
      }
    }
  }

  for (const auto& [display_number, frame] : latest_frames) {
    callback(display_number, frame.info.width, frame.info.height,
             frame.info.fourcc_format, frame.info.stride_bytes, frame.pixels);
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

Result<void> DisplayServerFrameSource::AttachShm(const DisplayMsg& msg) {
  CF_EXPECT(msg.shm_name[0] != '\0', "Missing shared memory name");
  Fd fd = CF_EXPECTF(Fd::ShmOpen(msg.shm_name, O_RDONLY, 0),
                     "Failed to open shared memory '{}'", msg.shm_name);
  const struct stat st =
      CF_EXPECTF(fd.Fstat(), "Failed to stat shared memory '{}'", msg.shm_name);
  const size_t size = static_cast<size_t>(st.st_size);
  CF_EXPECTF(size >= sizeof(ShmHeader),
             "Shared memory '{}' is too small: {} bytes", msg.shm_name, size);
  ScopedMMap mapping = fd.MMap(nullptr, size, PROT_READ, MAP_SHARED, 0);
  CF_EXPECTF(static_cast<bool>(mapping),
             "Failed to mmap shared memory '{}': {}", msg.shm_name,
             fd.StrError());
  const ShmHeader header = *static_cast<const ShmHeader*>(mapping.get());
  CF_EXPECTF(
      header.slot_size <= (size - sizeof(ShmHeader)) / kFrameSlotsPerDisplay,
      "Shared memory '{}' of {} bytes can't hold {} slots of {} bytes",
      msg.shm_name, size, kFrameSlotsPerDisplay, header.slot_size);
  CF_EXPECTF(
      static_cast<uint64_t>(header.frame.stride_bytes) * header.frame.height <=
          header.slot_size,
      "Frame of stride {} and height {} doesn't fit in a {} byte slot",
      header.frame.stride_bytes, header.frame.height, header.slot_size);

  MappedDisplay display = {
      .slot_size = header.slot_size,
      .frame_info = header.frame,
      .shm = std::move(mapping),
  };
  auto it = displays_.find(msg.display_number);
  if (it != displays_.end()) {
    display.width = it->second.width;
    display.height = it->second.height;
    displays_.erase(it);
  }
  displays_.emplace(msg.display_number, std::move(display));
  return {};
}

Result<DisplayServerFrameSource::Frame>
DisplayServerFrameSource::GetFrameFromShm(MappedDisplay& display,
                                          uint32_t slot) {
  CF_EXPECT(static_cast<bool>(display.shm), "No shared memory attached");
  CF_EXPECTF(slot < kFrameSlotsPerDisplay, "Invalid slot {}", slot);
  uint8_t* shm = static_cast<uint8_t*>(display.shm.get());
  return Frame{
      .info = display.frame_info,
      .pixels = shm + SlotOffset(display.slot_size, slot),
  };
}

void DisplayServerFrameSource::ReceiveLoop() {
  while (true) {
    std::array<PollSharedFd, 2> fds = {{
        {.fd = socket_,
         // NOLINTNEXTLINE(misc-include-cleaner): <poll.h>
         .events = POLLIN | POLLRDHUP | POLLHUP | POLLERR},
        {.fd = stop_event_, .events = POLLIN},
    }};
    if (SharedFD::Poll(fds.data(), fds.size(), -1) < 0) {
      if (errno == EINTR) {
        continue;
      }
      LOG(ERROR) << "Failed to poll the display server socket: " << StrError();
      break;
    }
    if (fds[1].revents & POLLIN) {
      // Stop event received.
      break;
    }
    if (fds[0].revents & (POLLRDHUP | POLLHUP | POLLERR)) {
      break;
    }
    if (!(fds[0].revents & POLLIN)) {
      continue;
    }

    alignas(kMsgAlignment) std::array<uint8_t, kMaxMsgSize> buffer = {};
    Result<uint64_t> bytes_read = socket_->Read(buffer.data(), buffer.size());
    if (!bytes_read.has_value()) {
      LOG(ERROR) << "Failed to read from the display server: "
                 << bytes_read.error();
      break;
    }
    if (*bytes_read == 0) {
      LOG(ERROR) << "The display server closed the connection";
      break;
    }
    Result<void> handled = HandleMessage(buffer.data(), *bytes_read);
    if (!handled.has_value()) {
      LOG(ERROR) << handled.error();
    }
  }
}

namespace {

template <typename T>
const T& ParseMsg(const void* data, size_t size) {
  if (size != sizeof(T)) {
    LOG(FATAL) << "Received a " << size
               << " byte message from the display server, expected "
               << sizeof(T) << " bytes";
  }
  return *static_cast<const T*>(data);
}

}  // namespace

Result<void> DisplayServerFrameSource::HandleMessage(const void* data,
                                                     size_t size) {
  if (size < sizeof(MsgType)) {
    LOG(FATAL) << "Received a " << size
               << " byte message from the display server";
  }
  const MsgType type = *static_cast<const MsgType*>(data);
  switch (type) {
    case MsgType::kDisplayCreated:
      OnDisplayCreated(ParseMsg<DisplayMsg>(data, size));
      return {};
    case MsgType::kDisplayDestroyed:
      OnDisplayDestroyed(ParseMsg<DisplayMsg>(data, size));
      return {};
    case MsgType::kDisplayShm:
      CF_EXPECT(OnDisplayShm(ParseMsg<DisplayMsg>(data, size)));
      return {};
    case MsgType::kFrame:
      CF_EXPECT(OnFrame(ParseMsg<FrameMsg>(data, size)));
      return {};
  }
  LOG(WARNING) << "Ignoring unknown display server message type "
               << static_cast<uint32_t>(type);
  return {};
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
    std::lock_guard<std::mutex> delivery_lock(frame_delivery_mutex_);
    std::lock_guard<std::mutex> lock(callback_mutex_);
    displays_.erase(msg.display_number);
    callback = display_event_callback_;
  }
  if (callback) {
    callback(DisplayDestroyedEvent{.display_number = msg.display_number});
  }
}

Result<void> DisplayServerFrameSource::OnDisplayShm(const DisplayMsg& msg) {
  DisplayMsg terminated = msg;
  terminated.shm_name[sizeof(terminated.shm_name) - 1] = '\0';

  std::lock_guard<std::mutex> delivery_lock(frame_delivery_mutex_);
  std::lock_guard<std::mutex> lock(callback_mutex_);
  CF_EXPECTF(AttachShm(terminated),
             "Failed to attach shared memory for display {}",
             msg.display_number);
  return {};
}

Result<void> DisplayServerFrameSource::OnFrame(const FrameMsg& msg) {
  std::lock_guard<std::mutex> delivery_lock(frame_delivery_mutex_);
  FrameCallback callback;
  Frame frame = {};
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    auto it = displays_.find(msg.display_number);
    CF_EXPECTF(it != displays_.end(), "Received a frame for unknown display {}",
               msg.display_number);
    frame = CF_EXPECTF(GetFrameFromShm(it->second, msg.slot),
                       "Invalid frame for display {}", msg.display_number);
    it->second.last_slot = msg.slot;
    if (!frame_callback_) {
      return {};
    }
    callback = frame_callback_;
  }
  callback(msg.display_number, frame.info.width, frame.info.height,
           frame.info.fourcc_format, frame.info.stride_bytes, frame.pixels);
  return {};
}

}  // namespace cuttlefish::display_server
