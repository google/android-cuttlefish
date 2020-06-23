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

#include "virtual_power_button.h"

namespace cuttlefish_input_service {

VirtualPowerButton::VirtualPowerButton()
    : VirtualDeviceBase("VSoC power button", 0x6007) {}

const std::vector<const uint32_t>& VirtualPowerButton::GetEventTypes() const {
  static const std::vector<const uint32_t> evt_types{EV_KEY};
  return evt_types;
}
const std::vector<const uint32_t>& VirtualPowerButton::GetKeys() const {
  static const std::vector<const uint32_t> keys{KEY_POWER};
  return keys;
}

}  // namespace cuttlefish_input_service
