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

#include "host/libs/screen_connector/screen_connector.h"

#include <android-base/logging.h>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/screen_connector/socket_based_screen_connector.h"
#include "host/libs/screen_connector/wayland_screen_connector.h"

namespace cuttlefish {

ScreenConnector* ScreenConnector::Get(int frames_fd) {
  auto config = cuttlefish::CuttlefishConfig::Get();
  if (config->gpu_mode() == cuttlefish::kGpuModeDrmVirgl ||
      config->gpu_mode() == cuttlefish::kGpuModeGfxStream) {
    return new WaylandScreenConnector(frames_fd);
  } else if (config->gpu_mode() == cuttlefish::kGpuModeGuestSwiftshader) {
    return new SocketBasedScreenConnector(frames_fd);
  } else {
      LOG(ERROR) << "Invalid gpu mode: " << config->gpu_mode();
      return nullptr;
  }
}

}  // namespace cuttlefish
