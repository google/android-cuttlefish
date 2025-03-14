/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include "host/libs/config/adb/adb.h"

#include <algorithm>
#include <string>

namespace cuttlefish {

AdbMode StringToAdbMode(const std::string& mode_cased) {
  std::string mode = mode_cased;
  std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
  if (mode == "vsock_tunnel") {
    return AdbMode::VsockTunnel;
  } else if (mode == "vsock_half_tunnel") {
    return AdbMode::VsockHalfTunnel;
  } else if (mode == "native_vsock") {
    return AdbMode::NativeVsock;
  } else {
    return AdbMode::Unknown;
  }
}

std::string AdbModeToString(AdbMode mode) {
  switch (mode) {
    case AdbMode::VsockTunnel:
      return "vsock_tunnel";
    case AdbMode::VsockHalfTunnel:
      return "vsock_half_tunnel";
    case AdbMode::NativeVsock:
      return "native_vsock";
    case AdbMode::Unknown:  // fall through
    default:
      return "unknown";
  }
}

}  // namespace cuttlefish
