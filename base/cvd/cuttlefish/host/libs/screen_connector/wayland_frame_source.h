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

#pragma once

#include <memory>

#include "cuttlefish/host/libs/screen_connector/frame_source.h"
#include "cuttlefish/host/libs/wayland/wayland_server.h"
#include "cuttlefish/host/libs/wayland/wayland_server_callbacks.h"

namespace cuttlefish {

// A FrameSource that runs the wayland compositor in-process, serving crosvm's
// wayland display socket directly.
class WaylandFrameSource : public FrameSource {
 public:
  // Takes ownership of `wayland_socket_fd`.
  WaylandFrameSource(int wayland_socket_fd, bool wayland_frames_are_rgba);

  void SetFrameCallback(FrameCallback callback) override;
  void SetDisplayEventCallback(DisplayEventCallback callback) override;

 private:
  std::unique_ptr<wayland::WaylandServer> server_;
};

}  // namespace cuttlefish
