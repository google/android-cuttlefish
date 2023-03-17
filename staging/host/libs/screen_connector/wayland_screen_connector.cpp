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

#include "host/libs/screen_connector/wayland_screen_connector.h"

#include <unistd.h>
#include <fcntl.h>

#include <android-base/logging.h>

#include "host/libs/wayland/wayland_server.h"

namespace cuttlefish {

WaylandScreenConnector::WaylandScreenConnector(ANNOTATED(FramesFd, int)
                                                   frames_fd) {
  int wayland_fd = fcntl(frames_fd, F_DUPFD_CLOEXEC, 3);
  CHECK(wayland_fd != -1) << "Unable to dup server, errno " << errno;
  close(frames_fd);

  server_.reset(new wayland::WaylandServer(wayland_fd));
}

void WaylandScreenConnector::SetFrameCallback(
    GenerateProcessedFrameCallbackImpl frame_callback) {
  server_->SetFrameCallback(std::move(frame_callback));
}

void WaylandScreenConnector::SetDisplayEventCallback(
    DisplayEventCallback event_callback) {
  server_->SetDisplayEventCallback(std::move(event_callback));
}

}  // namespace cuttlefish
