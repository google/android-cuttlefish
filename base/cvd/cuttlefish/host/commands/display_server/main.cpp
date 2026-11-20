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

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>  // IWYU pragma: keep: SOL_SOCKET, SO_SNDBUF
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/log/log.h"
#include "fmt/format.h"
#include "gflags/gflags.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/scoped_mmap.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/files/directory_contents.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/logging.h"
#include "cuttlefish/host/libs/display_server/protocol.h"
#include "cuttlefish/host/libs/wayland/wayland_server.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result_type.h"

DEFINE_int32(frame_server_fd, -1,
             "A listening socket that crosvm connects to as a wayland client.");
DEFINE_int32(display_server_fd, -1,
             "A listening socket fd that frame consumers connect to.");

namespace cuttlefish {
namespace {

using display_server::DisplayMsg;
using display_server::FrameInfo;
using display_server::FrameMsg;
using display_server::kFrameSlotsPerDisplay;
using display_server::MsgType;
using display_server::ShmHeader;
using display_server::SlotOffset;

std::string ShmNamePrefix(const CuttlefishConfig::InstanceSpecific& instance) {
  return fmt::format("cf_disp_{}_{}_", getuid(), instance.id());
}

void RemoveStaleShm(const std::string& prefix) {
  Result<std::vector<std::string>> names = DirectoryContents("/dev/shm");
  if (!names.has_value()) {
    LOG(WARNING) << "Failed to look for stale shared memory: " << names.error();
    return;
  }
  for (const std::string& name : *names) {
    if (name.starts_with(prefix)) {
      shm_unlink(("/" + name).c_str());
    }
  }
}

class FramePublisher {
 public:
  FramePublisher(SharedFD server_socket, std::string shm_name_prefix)
      : server_socket_(std::move(server_socket)),
        shm_name_prefix_(std::move(shm_name_prefix)) {}

  ~FramePublisher() {
    for (const auto& [_, display] : displays_) {
      if (!display.shm_name.empty()) {
        shm_unlink(display.shm_name.c_str());
      }
    }
  }

  void OnDisplayEvent(const DisplayEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const DisplayCreatedEvent* created =
            std::get_if<DisplayCreatedEvent>(&event)) {
      OnDisplayCreated(*created);
    } else if (const DisplayDestroyedEvent* destroyed =
                   std::get_if<DisplayDestroyedEvent>(&event)) {
      OnDisplayDestroyed(*destroyed);
    }
  }

  void OnFrame(uint32_t display_number, uint32_t width, uint32_t height,
               uint32_t fourcc_format, uint32_t stride_bytes, uint8_t* pixels) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!PrepareShmForFrame(display_number, width, height, fourcc_format,
                            stride_bytes)) {
      return;
    }
    Display& display = displays_[display_number];
    const uint32_t slot = display.next_slot++ % kFrameSlotsPerDisplay;
    uint8_t* shm = static_cast<uint8_t*>(display.shm.get());
    memcpy(shm + SlotOffset(display.slot_size, slot), pixels,
           display.slot_size);
    display.last_slot = slot;
    display.has_frame = true;
    BroadcastMsg(FrameMsg{
        .type = MsgType::kFrame,
        .display_number = display_number,
        .slot = slot,
    });
  }

  // Handles clients. Only returns on error.
  void ServerLoop() {
    while (true) {
      std::vector<PollSharedFd> fds = {
          // NOLINTNEXTLINE(misc-include-cleaner): <poll.h>
          {.fd = server_socket_, .events = POLLIN},
      };
      {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const SharedFD& client : clients_) {
          fds.push_back({.fd = client,
                         // NOLINTNEXTLINE(misc-include-cleaner): <poll.h>
                         .events = POLLRDHUP | POLLHUP | POLLERR});
        }
      }
      if (SharedFD::Poll(fds, -1) < 0) {
        if (errno == EINTR) {
          continue;
        }
        LOG(ERROR) << "Failed to poll the display server sockets: "
                   << StrError();
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (fds[0].revents & POLLIN) {
        AcceptClient();
      }
      for (size_t i = 1; i < fds.size(); ++i) {
        if (fds[i].revents & (POLLRDHUP | POLLHUP | POLLERR)) {
          std::erase(clients_, fds[i].fd);
        }
      }
    }
  }

 private:
  struct Display {
    uint32_t width = 0, height = 0, fourcc_format = 0, stride_bytes = 0;
    size_t slot_size = 0;
    uint32_t next_slot = 0, last_slot = 0;
    bool created_sent = false;
    bool has_frame = false;
    std::string shm_name;
    ScopedMMap shm;
  };

  static DisplayMsg MakeMsg(MsgType type, uint32_t display_number,
                            const Display& display) {
    DisplayMsg msg = {
        .type = type,
        .display_number = display_number,
        .width = display.width,
        .height = display.height,
        .shm_name = {},
    };
    strncpy(msg.shm_name, display.shm_name.c_str(), sizeof(msg.shm_name) - 1);
    return msg;
  }

  void OnDisplayCreated(const DisplayCreatedEvent& event) {
    Display& display = displays_[event.display_number];
    display.width = event.display_width;
    display.height = event.display_height;
    display.created_sent = true;
    BroadcastMsg(
        MakeMsg(MsgType::kDisplayCreated, event.display_number, display));
  }

  void OnDisplayDestroyed(const DisplayDestroyedEvent& event) {
    auto it = displays_.find(event.display_number);
    if (it == displays_.end()) {
      return;
    }
    if (!it->second.shm_name.empty()) {
      shm_unlink(it->second.shm_name.c_str());
    }
    BroadcastMsg(
        MakeMsg(MsgType::kDisplayDestroyed, event.display_number, it->second));
    displays_.erase(it);
  }

  template <typename T>
  void BroadcastMsg(const T& msg) {
    std::erase_if(clients_, [&](const SharedFD& client) {
      const ssize_t sent = client->Send(&msg, sizeof(msg), MSG_NOSIGNAL);
      return sent < 0 && client->GetErrno() != EAGAIN &&
             client->GetErrno() != EWOULDBLOCK;
    });
  }

  bool PrepareShmForFrame(uint32_t display_number, uint32_t width,
                          uint32_t height, uint32_t fourcc_format,
                          uint32_t stride_bytes) {
    auto it = displays_.find(display_number);
    if (it != displays_.end() && it->second.shm && it->second.width == width &&
        it->second.height == height &&
        it->second.fourcc_format == fourcc_format &&
        it->second.stride_bytes == stride_bytes) {
      return true;
    }
    bool need_notify =
        (it == displays_.end() || !it->second.created_sent ||
         it->second.width != width || it->second.height != height);
    if (it != displays_.end()) {
      if (!it->second.shm_name.empty()) {
        shm_unlink(it->second.shm_name.c_str());
      }
      displays_.erase(it);
    }
    std::string shm_name = fmt::format("/{}{}_{}", shm_name_prefix_,
                                       display_number, ++shm_generation_);
    const size_t slot_size = static_cast<size_t>(stride_bytes) * height;
    const size_t shm_size = SlotOffset(slot_size, kFrameSlotsPerDisplay);
    Result<Fd, int> fd =
        Fd::ShmOpen(shm_name, O_CREAT | O_RDWR | O_TRUNC, 0600);
    ScopedMMap mapping =
        (fd.has_value() && fd->Truncate(shm_size).has_value())
            ? fd->MMap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, 0)
            : ScopedMMap();
    if (!mapping) {
      shm_unlink(shm_name.c_str());
      return false;
    }
    *static_cast<ShmHeader*>(mapping.get()) = ShmHeader{
        .slot_size = slot_size,
        .frame =
            FrameInfo{
                .width = width,
                .height = height,
                .fourcc_format = fourcc_format,
                .stride_bytes = stride_bytes,
            },
    };
    Display display = {.width = width,
                       .height = height,
                       .fourcc_format = fourcc_format,
                       .stride_bytes = stride_bytes,
                       .slot_size = slot_size,
                       .created_sent = true,
                       .shm_name = std::move(shm_name),
                       .shm = std::move(mapping)};
    if (need_notify) {
      BroadcastMsg(MakeMsg(MsgType::kDisplayCreated, display_number, display));
    }
    BroadcastMsg(MakeMsg(MsgType::kDisplayShm, display_number, display));
    displays_.emplace(display_number, std::move(display));
    return true;
  }

  void AcceptClient() {
    Result<Fd> accepted = Fd::Accept(*server_socket_);
    if (!accepted.has_value()) {
      return;
    }
    SharedFD client_fd = std::move(*accepted);
    client_fd->Fcntl(F_SETFL, O_NONBLOCK);
    int buf_size = 256 * 1024;
    // NOLINTNEXTLINE(misc-include-cleaner): <sys/socket.h>
    client_fd->SetSockOpt(SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    SendCurrentState(client_fd);
    clients_.push_back(std::move(client_fd));
  }

  void SendCurrentState(const SharedFD& client) {
    for (const auto& [display_number, display] : displays_) {
      DisplayMsg created_msg =
          MakeMsg(MsgType::kDisplayCreated, display_number, display);
      (void)client->Send(&created_msg, sizeof(created_msg), MSG_NOSIGNAL);
      if (display.shm) {
        DisplayMsg shm_msg =
            MakeMsg(MsgType::kDisplayShm, display_number, display);
        (void)client->Send(&shm_msg, sizeof(shm_msg), MSG_NOSIGNAL);
      }
      if (display.has_frame) {
        FrameMsg frame_msg = {
            .type = MsgType::kFrame,
            .display_number = display_number,
            .slot = display.last_slot,
        };
        (void)client->Send(&frame_msg, sizeof(frame_msg), MSG_NOSIGNAL);
      }
    }
  }

  SharedFD server_socket_;
  const std::string shm_name_prefix_;
  std::mutex mutex_;
  uint32_t shm_generation_ = 0;
  std::vector<SharedFD> clients_;
  std::map<uint32_t, Display> displays_;
};

int DisplayServerMain() {
  const CuttlefishConfig* config = CuttlefishConfig::Get();
  if (!config || FLAGS_frame_server_fd < 0 || FLAGS_display_server_fd < 0) {
    return 1;
  }
  const auto instance = config->ForDefaultInstance();
  SharedFD client_server = SharedFD::Dup(FLAGS_display_server_fd);
  close(FLAGS_display_server_fd);
  if (!client_server->IsOpen()) {
    LOG(ERROR) << "Unable to dup display server fd: "
               << client_server->StrError();
    return 1;
  }

  const std::string shm_name_prefix = ShmNamePrefix(instance);
  RemoveStaleShm(shm_name_prefix);

  FramePublisher publisher(client_server, shm_name_prefix);
  wayland::WaylandServer wayland_server(
      FLAGS_frame_server_fd, !instance.guest_uses_bgra_framebuffers());
  wayland_server.SetFrameCallback([&](uint32_t display_number, uint32_t width,
                                      uint32_t height, uint32_t fourcc_format,
                                      uint32_t stride_bytes, uint8_t* pixels) {
    publisher.OnFrame(display_number, width, height, fourcc_format,
                      stride_bytes, pixels);
  });
  wayland_server.SetDisplayEventCallback(
      [&](const DisplayEvent& event) { publisher.OnDisplayEvent(event); });

  publisher.ServerLoop();
  return 1;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  return cuttlefish::DisplayServerMain();
}
