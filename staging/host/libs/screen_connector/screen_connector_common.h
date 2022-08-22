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

#pragma once

#include <cstdint>
#include <functional>

#include <android-base/logging.h>

#include "common/libs/utils/size_utils.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

template<typename T>
struct is_movable {
  static constexpr const bool value =
      std::is_move_constructible<T>::value &&
      std::is_move_assignable<T>::value;
};

// this callback type is going directly to socket-based or wayland ScreenConnector
using GenerateProcessedFrameCallbackImpl =
    std::function<void(std::uint32_t /*display_number*/,      //
                       std::uint32_t /*frame_width*/,         //
                       std::uint32_t /*frame_height*/,        //
                       std::uint32_t /*frame_stride_bytes*/,  //
                       std::uint8_t* /*frame_pixels*/)>;

struct ScreenConnectorInfo {
  // functions are intended to be inlined
  static constexpr std::uint32_t BytesPerPixel() { return 4; }
  static std::uint32_t ScreenCount() {
    auto config = ChkAndGetConfig();
    auto instance = config->ForDefaultInstance();
    auto display_configs = instance.display_configs();
    return static_cast<std::uint32_t>(display_configs.size());
  }
  static std::uint32_t ScreenHeight(std::uint32_t display_number) {
    auto config = ChkAndGetConfig();
    auto instance = config->ForDefaultInstance();
    auto display_configs = instance.display_configs();
    CHECK_GT(display_configs.size(), display_number);
    return display_configs[display_number].height;
  }
  static std::uint32_t ScreenWidth(std::uint32_t display_number) {
    auto config = ChkAndGetConfig();
    auto instance = config->ForDefaultInstance();
    auto display_configs = instance.display_configs();
    CHECK_GE(display_configs.size(), display_number);
    return display_configs[display_number].width;
  }
  static std::uint32_t ComputeScreenStrideBytes(const std::uint32_t w) {
    return AlignToPowerOf2(w * BytesPerPixel(), 4);
  }
  static std::uint32_t ComputeScreenSizeInBytes(const std::uint32_t w,
                                                const std::uint32_t h) {
    return ComputeScreenStrideBytes(w) * h;
  }
  static std::uint32_t ScreenStrideBytes(const std::uint32_t display_number) {
    return ComputeScreenStrideBytes(ScreenWidth(display_number));
  }
  static std::uint32_t ScreenSizeInBytes(const std::uint32_t display_number) {
    return ComputeScreenStrideBytes(ScreenWidth(display_number)) *
           ScreenHeight(display_number);
  }

 private:
  static auto ChkAndGetConfig() -> decltype(cuttlefish::CuttlefishConfig::Get()) {
    auto config = cuttlefish::CuttlefishConfig::Get();
    CHECK(config) << "Config is Missing";
    return config;
  }
};

struct ScreenConnectorFrameRenderer {
  virtual bool RenderConfirmationUi(std::uint32_t display_number,
                                    std::uint32_t frame_width,
                                    std::uint32_t frame_height,
                                    std::uint32_t frame_stride_bytes,
                                    std::uint8_t* frame_bytes) = 0;
  virtual bool IsCallbackSet() const = 0;
  virtual ~ScreenConnectorFrameRenderer() = default;
};

// this is inherited by the data type that represents the processed frame
// being moved around.
struct ScreenConnectorFrameInfo {
  std::uint32_t display_number_;
  bool is_success_;
};

}  // namespace cuttlefish
