/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "common/libs/utils/device_type.h"

namespace cuttlefish {

// Parse device type from android-info.txt config field.
DeviceType ParseDeviceType(std::string_view type_name) {
  if (type_name == "phone") {
    return DeviceType::Phone;
  } else if (type_name == "wear") {
    return DeviceType::Wear;
  } else if (type_name == "auto" || type_name == "auto_portrait" ||
             type_name == "auto_dd" || type_name == "auto_md") {
    return DeviceType::Auto;
  } else if (type_name == "foldable") {
    return DeviceType::Foldable;
  } else if (type_name == "tv") {
    return DeviceType::Tv;
  } else if (type_name == "minidroid") {
    return DeviceType::Minidroid;
  } else if (type_name == "go") {
    return DeviceType::Go;
  } else {
    return DeviceType::Unknown;
  }
}

}  // namespace cuttlefish
