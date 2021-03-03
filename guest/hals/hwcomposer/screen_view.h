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
#include <sys/time.h>

namespace cuttlefish {

struct CompositionStats {
  uint32_t num_prepare_calls;
  uint16_t num_layers;
  uint16_t num_hwcomposited_layers;
  timespec last_vsync;
  timespec prepare_start;
  timespec prepare_end;
  timespec set_start;
  timespec set_end;
};

class ScreenView {
 public:
  ScreenView() = default;
  virtual ~ScreenView() = default;

  ScreenView(const ScreenView&) = delete;
  ScreenView& operator=(const ScreenView&) = delete;

  // Gets the buffer for the next frame that should be sent to the host.
  virtual std::uint8_t* AcquireNextBuffer(std::uint32_t display_number) = 0;

  // Mark that the next buffer has been populated with the next frame and is
  // ready to be sent to the host.
  virtual void PresentAcquiredBuffer(std::uint32_t display_number) = 0;

  static std::uint32_t ScreenCount();
  static std::uint32_t ScreenWidth(std::uint32_t display_number);
  static std::uint32_t ScreenHeight(std::uint32_t display_number);
  static std::uint32_t ScreenDPI(std::uint32_t display_number);
  static std::uint32_t ScreenRefreshRateHz(std::uint32_t display_number);
  static std::uint32_t ScreenStrideBytes(std::uint32_t display_number);
  static std::uint32_t ScreenSizeBytes(std::uint32_t display_number);
  static constexpr std::uint32_t ScreenBytesPerPixel() { return 4; }
};
}  // namespace cuttlefish