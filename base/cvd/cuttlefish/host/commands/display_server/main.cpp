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
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "absl/log/log.h"
#include "fmt/format.h"
#include "gflags/gflags.h"

#include "cuttlefish/common/libs/fs/scoped_mmap.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/logging.h"
#include "cuttlefish/host/libs/display_server/protocol.h"
#include "cuttlefish/host/libs/wayland/wayland_server.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"
#include "cuttlefish/result/result_type.h"

DEFINE_int32(frame_server_fd, -1,
             "A listening socket that crosvm connects to as a wayland client.");

namespace cuttlefish {
namespace {

using display_server::ClientCommand;
using display_server::DisplayMsg;
using display_server::kFrameSlotsPerDisplay;
using display_server::MsgType;

class FramePublisher {
 public:
  explicit FramePublisher(SharedFD server_socket)
      : server_socket_(std::move(server_socket)),
        server_thread_([this]() { ServerLoop(); }) {}

  ~FramePublisher() {
    running_ = false;
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
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
    if (!PrepareShmForFrame(display_number, width, height, stride_bytes)) {
      return;
    }
    Display& display = displays_[display_number];
    display.fourcc_format = fourcc_format;
    const bool has_ready_clients =
        std::any_of(clients_.begin(), clients_.end(),
                    [](const Client& client) { return client.ready; });
    if (display.has_frame && !has_ready_clients) {
      return;
    }
    uint32_t slot = display.next_slot++ % kFrameSlotsPerDisplay;
    memcpy(static_cast<uint8_t*>(display.shm.get()) + slot * display.slot_size,
           pixels, display.slot_size);
    display.last_slot = slot;
    display.has_frame = true;
    BroadcastMsg(MakeMsg(MsgType::kFrame, display_number, display, slot));
  }

 private:
  struct Client {
    SharedFD fd;
    bool ready = false;
  };
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
                            const Display& display, uint32_t slot = 0) {
    DisplayMsg msg = {
        .type = type,
        .display_number = display_number,
        .width = display.width,
        .height = display.height,
        .fourcc_format = display.fourcc_format,
        .stride_bytes = display.stride_bytes,
        .slot = slot,
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

  void BroadcastMsg(const DisplayMsg& msg) {
    std::erase_if(clients_, [&](Client& client) {
      if (!client.ready) {
        return false;
      }
      Result<uint64_t> written = client.fd->Write(&msg, sizeof(msg));
      return !written.has_value() && client.fd->GetErrno() != EAGAIN &&
             client.fd->GetErrno() != EWOULDBLOCK;
    });
  }

  bool PrepareShmForFrame(uint32_t display_number, uint32_t width,
                          uint32_t height, uint32_t stride_bytes) {
    auto it = displays_.find(display_number);
    if (it != displays_.end() && it->second.shm && it->second.width == width &&
        it->second.height == height &&
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
    std::string shm_name = fmt::format("/cf_disp_{}_{}_{}", getpid(),
                                       display_number, ++shm_generation_);
    size_t slot_size = static_cast<size_t>(stride_bytes) * height;
    SharedFD fd = SharedFD::ShmOpen(shm_name, O_CREAT | O_RDWR | O_TRUNC, 0600);
    ScopedMMap mapping =
        (fd->IsOpen() &&
         fd->Truncate(slot_size * kFrameSlotsPerDisplay).has_value())
            ? fd->MMap(nullptr, slot_size * kFrameSlotsPerDisplay,
                       PROT_READ | PROT_WRITE, MAP_SHARED, 0)
            : ScopedMMap();
    if (!mapping) {
      shm_unlink(shm_name.c_str());
      return false;
    }
    Display display = {.width = width,
                       .height = height,
                       .stride_bytes = stride_bytes,
                       .slot_size = slot_size,
                       .created_sent = true,
                       .shm_name = std::move(shm_name),
                       .shm = std::move(mapping)};
    if (need_notify) {
      BroadcastMsg(MakeMsg(MsgType::kDisplayCreated, display_number, display));
    }
    displays_.emplace(display_number, std::move(display));
    return true;
  }

  void ServerLoop() {
    while (running_) {
      std::vector<PollSharedFd> fds = {
          // NOLINTNEXTLINE(misc-include-cleaner): <poll.h>
          {.fd = server_socket_, .events = POLLIN}};
      {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Client& client : clients_) {
          fds.push_back({.fd = client.fd,
                         // NOLINTNEXTLINE(misc-include-cleaner): <poll.h>
                         .events = POLLIN | POLLRDHUP | POLLHUP | POLLERR});
        }
      }
      if (SharedFD::Poll(fds, 250) <= 0 || !running_) {
        continue;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      if (fds[0].revents & POLLIN) {
        AcceptClient();
      }
      for (size_t i = 1; i < fds.size(); ++i) {
        if (!fds[i].revents) {
          continue;
        }
        auto it = std::find_if(
            clients_.begin(), clients_.end(),
            [&](const Client& client) { return client.fd == fds[i].fd; });
        if (it == clients_.end()) {
          continue;
        }
        if (fds[i].revents & (POLLRDHUP | POLLHUP | POLLERR)) {
          clients_.erase(it);
        } else if (fds[i].revents & POLLIN) {
          HandleClientCommand(*it);
        }
      }
    }
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
    clients_.push_back(Client{.fd = std::move(client_fd)});
  }

  void HandleClientCommand(Client& client) {
    ClientCommand command{};
    Result<uint64_t> bytes_read = client.fd->Read(&command, sizeof(command));
    if (!bytes_read.has_value() || *bytes_read != sizeof(command) ||
        command != ClientCommand::kReady) {
      return;
    }
    client.ready = true;
    SendCurrentState(client);
  }

  void SendCurrentState(Client& client) {
    for (const auto& [display_number, display] : displays_) {
      DisplayMsg created_msg =
          MakeMsg(MsgType::kDisplayCreated, display_number, display);
      (void)client.fd->Write(&created_msg, sizeof(created_msg));
      if (display.has_frame) {
        DisplayMsg frame_msg = MakeMsg(MsgType::kFrame, display_number, display,
                                       display.last_slot);
        (void)client.fd->Write(&frame_msg, sizeof(frame_msg));
      }
    }
  }

  SharedFD server_socket_;
  std::atomic<bool> running_{true};
  std::mutex mutex_;
  uint32_t shm_generation_ = 0;
  std::vector<Client> clients_;
  std::map<uint32_t, Display> displays_;
  std::thread server_thread_;
};

int DisplayServerMain() {
  const CuttlefishConfig* config = CuttlefishConfig::Get();
  if (!config || FLAGS_frame_server_fd < 0) {
    return 1;
  }
  const auto instance = config->ForDefaultInstance();
  SharedFD client_server = SharedFD::SocketLocalServer(
      instance.DisplayServerSocketPath(), false, SOCK_SEQPACKET, 0666);
  if (!client_server->IsOpen()) {
    LOG(ERROR) << client_server->StrError();
    return 1;
  }

  FramePublisher publisher(client_server);
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

  while (true) {
    pause();
  }
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  return cuttlefish::DisplayServerMain();
}
