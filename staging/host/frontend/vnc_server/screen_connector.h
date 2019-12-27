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

namespace cvd {
namespace vnc {

using FrameCallback = std::function<void(std::uint32_t /*frame_number*/,
                                         std::uint8_t* /*frame_pixels*/)>;

class ScreenConnector {
 public:
  static ScreenConnector* Get();

  virtual ~ScreenConnector() = default;

  // Runs the given callback on the next available frame after the given
  // frame number and returns true if successful.
  virtual bool OnFrameAfter(std::uint32_t frame_number,
                            const FrameCallback& frame_callback) = 0;

 protected:
  ScreenConnector() = default;
};

}  // namespace vnc
}  // namespace cvd