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

#include <cstdint>
#include <functional>

#include "common/libs/utils/size_utils.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

using FrameCallback = std::function<void(std::uint32_t /*display_number*/,
                                         std::uint8_t* /*frame_pixels*/)>;

class ScreenConnector {
 public:
  static ScreenConnector* Get(int frames_fd);

  virtual ~ScreenConnector() = default;

  // Runs the given callback on the next available frame and returns true if
  // successful.
  virtual bool OnNextFrame(const FrameCallback& frame_callback) = 0;

  // Let the screen connector know when there are clients connected
  virtual void ReportClientsConnected(bool have_clients);

  static constexpr std::uint32_t BytesPerPixel() { return 4; }
  static std::uint32_t ScreenCount();
  static std::uint32_t ScreenHeight(std::uint32_t display_number);
  static std::uint32_t ScreenWidth(std::uint32_t display_number);
  static std::uint32_t ScreenStrideBytes(std::uint32_t display_number);
  static std::uint32_t ScreenSizeInBytes(std::uint32_t display_number);

 protected:
  ScreenConnector() = default;
};

}  // namespace cuttlefish
