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

#pragma once

#include "host/libs/screen_connector/screen_connector.h"

#include <memory>

#include "host/libs/wayland/wayland_server.h"

namespace cuttlefish {

class WaylandScreenConnector : public ScreenConnector {
 public:
  WaylandScreenConnector(int frames_fd);

  bool OnFrameAfter(std::uint32_t frame_number,
                    const FrameCallback& frame_callback) override;

 private:
  std::unique_ptr<wayland::WaylandServer> server_;
};

}