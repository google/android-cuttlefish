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
#pragma once

#include <ostream>
#include <string_view>

namespace cuttlefish {

enum class DeviceType {
  Unknown = 0,
  Phone,
  Wear,
  Auto,
  Foldable,
  Tv,
  Minidroid,
  Go,
};

// Parse device type android-info.txt config field.
DeviceType ParseDeviceType(std::string_view type_name);
std::string_view DeviceTypeToStringView(DeviceType);

std::ostream& operator<<(std::ostream&, DeviceType);

template <typename Sink>
void AbslStringify(Sink& sink, DeviceType device_type) {
  sink.Append(DeviceTypeToStringView(device_type));
}

// for libfmt
std::string_view format_as(DeviceType);

}  // namespace cuttlefish
