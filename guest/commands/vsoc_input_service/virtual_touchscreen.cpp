/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "virtual_touchscreen.h"

namespace cuttlefish_input_service {

const std::vector<const uint32_t>& VirtualTouchScreen::GetEventTypes() const {
  static const std::vector<const uint32_t> evt_types{EV_ABS, EV_KEY};
  return evt_types;
}
const std::vector<const uint32_t>& VirtualTouchScreen::GetKeys() const {
  static const std::vector<const uint32_t> keys{BTN_TOUCH};
  return keys;
}
const std::vector<const uint32_t>& VirtualTouchScreen::GetProperties() const {
  static const std::vector<const uint32_t> properties{INPUT_PROP_DIRECT};
  return properties;
}
const std::vector<const uint32_t>& VirtualTouchScreen::GetAbs() const {
  static const std::vector<const uint32_t> abs{ABS_X, ABS_Y};
  return abs;
}

VirtualTouchScreen::VirtualTouchScreen(uint32_t width, uint32_t height)
    : VirtualDeviceBase("VSoC touchscreen", 0x6006) {
  dev_.absmin[ABS_X] = 0;
  dev_.absmax[ABS_X] = width;
  dev_.absmin[ABS_Y] = 0;
  dev_.absmax[ABS_Y] = height;
}

}  // namespace cuttlefish_input_service
