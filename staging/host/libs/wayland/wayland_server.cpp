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

#include "host/libs/wayland/wayland_server.h"

#include <android-base/logging.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "host/libs/wayland/wayland_compositor.h"
#include "host/libs/wayland/wayland_dmabuf.h"
#include "host/libs/wayland/wayland_seat.h"
#include "host/libs/wayland/wayland_shell.h"
#include "host/libs/wayland/wayland_subcompositor.h"
#include "host/libs/wayland/wayland_surface.h"
#include "host/libs/wayland/wayland_utils.h"

namespace wayland {
namespace internal {

struct WaylandServerState {
  struct wl_display* display_ = nullptr;

  Surface surface_;
};

}  // namespace internal

WaylandServer::WaylandServer(int wayland_socket_fd) {
  server_thread_ =
      std::thread(
          [this, wayland_socket_fd]() {
            ServerLoop(wayland_socket_fd);
          });

  std::unique_lock<std::mutex> lock(server_ready_mutex_);
  server_ready_cv_.wait(lock, [&]{return server_ready_; });
}

WaylandServer::~WaylandServer() {
  wl_display_terminate(server_state_->display_);
  server_thread_.join();
}

void WaylandServer::ServerLoop(int fd) {
  server_state_.reset(new internal::WaylandServerState());

  server_state_->display_ = wl_display_create();
  CHECK(server_state_->display_ != nullptr)
    << "Failed to start WaylandServer: failed to create display";

  if (fd < 0) {
    const char* socket = wl_display_add_socket_auto(server_state_->display_);
    CHECK(socket != nullptr)
        << "Failed to start WaylandServer: failed to create socket";

    LOG(INFO) << "WaylandServer running on socket " << socket;
  } else {
    CHECK(wl_display_add_socket_fd(server_state_->display_, fd) == 0)
        << "Failed to start WaylandServer: failed to use fd " << fd;

    LOG(INFO) << "WaylandServer running on socket " << fd;
  }

  wl_display_init_shm(server_state_->display_);

  BindCompositorInterface(server_state_->display_, &server_state_->surface_);
  BindDmabufInterface(server_state_->display_);
  BindSubcompositorInterface(server_state_->display_);
  BindSeatInterface(server_state_->display_);
  BindShellInterface(server_state_->display_);

  {
    std::lock_guard<std::mutex> lock(server_ready_mutex_);
    server_ready_ = true;
  }
  server_ready_cv_.notify_one();

  wl_display_run(server_state_->display_);
  wl_display_destroy(server_state_->display_);
}

std::future<void> WaylandServer::OnFrameAfter(
    uint32_t frame_number,
    const FrameCallback& frame_callback) {
  // Wraps the given callback in a callback that can be waited on using
  // std::future.
  Surface::FrameCallbackPackaged packaged([&frame_callback](std::uint32_t frame_number,
                                                   std::uint8_t* frame_pixels){
    frame_callback(frame_number, frame_pixels);
  });

  std::future<void> frame_callback_complete = packaged.get_future();

  server_state_->surface_.OnFrameAfter(frame_number, std::move(packaged));
  return frame_callback_complete;
}

}  // namespace wayland