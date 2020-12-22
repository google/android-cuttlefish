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

#include "guest/hals/hwcomposer/screen_view.h"

#include <android-base/logging.h>
#include <log/log.h>

#include "common/libs/device_config/device_config.h"
#include "common/libs/utils/size_utils.h"

namespace cuttlefish {
namespace {

const DeviceConfig& GetDeviceConfig() {
  static std::unique_ptr<DeviceConfig> device_config =
    []() {
      auto device_config_helper = DeviceConfigHelper::Get();
      CHECK(device_config_helper);

      DeviceConfig* device_config = new DeviceConfig();
      *device_config = device_config_helper->GetDeviceConfig();

      return std::unique_ptr<DeviceConfig>(device_config);
    }();
  return *device_config;
}

}  // namespace

/*static*/ std::uint32_t ScreenView::ScreenCount() {
  const auto& device_config = GetDeviceConfig();
  return device_config.display_config_size();
}

/*static*/ std::uint32_t ScreenView::ScreenWidth(std::uint32_t display_number) {
  const auto& device_config = GetDeviceConfig();
  CHECK_GE(device_config.display_config_size(), display_number);
  const auto& display_config = device_config.display_config(display_number);
  return display_config.width();
}

/*static*/ std::uint32_t ScreenView::ScreenHeight(std::uint32_t display_number) {
  const auto& device_config = GetDeviceConfig();
  CHECK_GE(device_config.display_config_size(), display_number);
  const auto& display_config = device_config.display_config(display_number);
  return display_config.height();
}

/*static*/ std::uint32_t ScreenView::ScreenDPI(std::uint32_t display_number) {
  const auto& device_config = GetDeviceConfig();
  CHECK_GE(device_config.display_config_size(), display_number);
  const auto& display_config = device_config.display_config(display_number);
  return display_config.dpi();
}

/*static*/ std::uint32_t ScreenView::ScreenRefreshRateHz(
    std::uint32_t display_number) {
  const auto& device_config = GetDeviceConfig();
  CHECK_GE(device_config.display_config_size(), display_number);
  const auto& display_config = device_config.display_config(display_number);
  return display_config.refresh_rate_hz();
}

/*static*/ std::uint32_t ScreenView::ScreenStrideBytes(
    std::uint32_t display_number) {
  return AlignToPowerOf2(ScreenWidth(display_number) * ScreenBytesPerPixel(), 4);
}

/*static*/ std::uint32_t ScreenView::ScreenSizeBytes(
    std::uint32_t display_num) {
  static constexpr const std::uint32_t kMysteriousSwiftShaderPadding = 4;
  return ScreenStrideBytes(display_num) * ScreenHeight(display_num) + kMysteriousSwiftShaderPadding;
}

int ScreenView::NextBuffer() {
  int num_buffers = this->num_buffers();
  last_buffer_ = num_buffers > 0 ? (last_buffer_ + 1) % num_buffers : -1;
  return last_buffer_;
}

}  // namespace cuttlefish
