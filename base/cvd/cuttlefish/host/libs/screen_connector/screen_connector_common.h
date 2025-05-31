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

#include <stdint.h>

#include <functional>
#include <type_traits>

namespace cuttlefish {

template <typename T>
struct is_movable {
  static constexpr const bool value =
      std::is_move_constructible<T>::value && std::is_move_assignable<T>::value;
};

// this callback type is going directly to socket-based or wayland
// ScreenConnector
using GenerateProcessedFrameCallbackImpl =
    std::function<void(uint32_t /*display_number*/,       //
                       uint32_t /*frame_width*/,          //
                       uint32_t /*frame_height*/,         //
                       uint32_t /*frame_fourcc_format*/,  //
                       uint32_t /*frame_stride_bytes*/,   //
                       uint8_t* /*frame_pixels*/)>;

namespace ScreenConnectorInfo {

uint32_t ScreenHeight(uint32_t display_number);
uint32_t ScreenWidth(uint32_t display_number);
uint32_t ComputeScreenStrideBytes(uint32_t w);
uint32_t ComputeScreenSizeInBytes(uint32_t w, uint32_t h);

}  // namespace ScreenConnectorInfo

struct ScreenConnectorFrameRenderer {
  virtual bool RenderConfirmationUi(uint32_t display_number,
                                    uint32_t frame_width, uint32_t frame_height,
                                    uint32_t frame_fourcc_format,
                                    uint32_t frame_stride_bytes,
                                    uint8_t* frame_bytes) = 0;
  virtual bool IsCallbackSet() const = 0;
  virtual ~ScreenConnectorFrameRenderer() = default;
};

// this is inherited by the data type that represents the processed frame
// being moved around.
struct ScreenConnectorFrameInfo {
  uint32_t display_number_;
  bool is_success_;
};

}  // namespace cuttlefish
