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

// Ignore by default
void ScreenConnector::ReportClientsConnected(bool /*have_clients*/) {}

/*static*/
std::uint32_t ScreenConnector::ScreenCount() {
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto display_configs = config->display_configs();
  return static_cast<std::uint32_t>(display_configs.size());
}

/*static*/
std::uint32_t ScreenConnector::ScreenHeight(std::uint32_t display_number) {
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto display_configs = config->display_configs();
  CHECK_GE(display_configs.size(), display_number);
  return display_configs[display_number].height;
}

/*static*/
std::uint32_t ScreenConnector::ScreenWidth(std::uint32_t display_number) {
  auto config = cuttlefish::CuttlefishConfig::Get();
  auto display_configs = config->display_configs();
  CHECK_GE(display_configs.size(), display_number);
  return display_configs[display_number].width;
}

/*static*/
std::uint32_t ScreenConnector::ScreenStrideBytes(std::uint32_t display_number) {
    return AlignToPowerOf2(ScreenWidth(display_number) * BytesPerPixel(), 4);
}

/*static*/
std::uint32_t ScreenConnector::ScreenSizeInBytes(std::uint32_t display_num) {
    return ScreenStrideBytes(display_num) * ScreenHeight(display_num);
}

}  // namespace cuttlefish
