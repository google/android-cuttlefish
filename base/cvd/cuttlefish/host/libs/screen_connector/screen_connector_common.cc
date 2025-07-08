/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "cuttlefish/host/libs/screen_connector/screen_connector_common.h"

#include <stdint.h>

#include <vector>

#include <android-base/logging.h>

#include "cuttlefish/common/libs/utils/size_utils.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

namespace ScreenConnectorInfo {

static std::vector<CuttlefishConfig::DisplayConfig> DisplayConfigs() {
  auto config = CuttlefishConfig::Get();
  CHECK(config) << "Config is Missing";
  return config->ForDefaultInstance().display_configs();
}

static constexpr uint32_t BytesPerPixel() { return 4; }

uint32_t ScreenHeight(uint32_t display_number) {
  std::vector<CuttlefishConfig::DisplayConfig> displays = DisplayConfigs();
  CHECK_GT(displays.size(), display_number);
  return displays[display_number].height;
}

uint32_t ScreenWidth(uint32_t display_number) {
  std::vector<CuttlefishConfig::DisplayConfig> displays = DisplayConfigs();
  CHECK_GT(displays.size(), display_number);
  return displays[display_number].width;
}

uint32_t ComputeScreenStrideBytes(const uint32_t w) {
  return AlignToPowerOf2(w * BytesPerPixel(), 4);
}

uint32_t ComputeScreenSizeInBytes(const uint32_t w, const uint32_t h) {
  return ComputeScreenStrideBytes(w) * h;
}

};  // namespace ScreenConnectorInfo

}  // namespace cuttlefish
