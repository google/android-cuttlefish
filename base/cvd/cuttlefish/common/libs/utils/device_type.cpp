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

#include "cuttlefish/common/libs/utils/device_type.h"

#include <string_view>

#include "absl/strings/match.h"

namespace cuttlefish {

// Parse device type from android-info.txt config field.
DeviceType ParseDeviceType(std::string_view type_name) {
  if (type_name == "phone") {
    return DeviceType::Phone;
  } else if (type_name == "wear") {
    return DeviceType::Wear;
  } else if (absl::StartsWith(type_name, "auto")) {
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

std::string_view DeviceTypeToStringView(DeviceType device_type) {
  switch (device_type) {
    case DeviceType::Unknown:
      return "unknown";
    case DeviceType::Phone:
      return "phone";
    case DeviceType::Wear:
      return "wear";
    case DeviceType::Auto:
      return "auto";
    case DeviceType::Foldable:
      return "foldable";
    case DeviceType::Tv:
      return "tv";
    case DeviceType::Minidroid:
      return "minidroid";
    case DeviceType::Go:
      return "go";
  }
}

std::ostream& operator<<(std::ostream& out, DeviceType device_type) {
  return out << DeviceTypeToStringView(device_type);
}

std::string_view format_as(DeviceType device_type) {
  return DeviceTypeToStringView(device_type);
}

}  // namespace cuttlefish
