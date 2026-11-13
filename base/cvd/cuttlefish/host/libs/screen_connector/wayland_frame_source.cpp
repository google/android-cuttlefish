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

#include "cuttlefish/host/libs/screen_connector/wayland_frame_source.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <memory>
#include <utility>

#include "absl/log/check.h"

#include "cuttlefish/host/libs/wayland/wayland_server.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"

namespace cuttlefish {

WaylandFrameSource::WaylandFrameSource(int wayland_socket_fd,
                                       bool wayland_frames_are_rgba) {
  const int wayland_fd = fcntl(wayland_socket_fd, F_DUPFD_CLOEXEC, 3);
  CHECK(wayland_fd != -1) << "Unable to dup server, errno " << errno;
  close(wayland_socket_fd);

  server_ = std::make_unique<wayland::WaylandServer>(wayland_fd,
                                                     wayland_frames_are_rgba);
}

void WaylandFrameSource::SetFrameCallback(FrameCallback callback) {
  server_->SetFrameCallback(std::move(callback));
}

void WaylandFrameSource::SetDisplayEventCallback(
    DisplayEventCallback callback) {
  server_->SetDisplayEventCallback(std::move(callback));
}

}  // namespace cuttlefish
