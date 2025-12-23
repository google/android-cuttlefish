/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/external_network_mode.h"

#include <cctype>
#include <ostream>
#include <string_view>

#include "absl/strings/match.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

std::ostream& operator<<(std::ostream& out, ExternalNetworkMode net) {
  switch (net) {
    case ExternalNetworkMode::kUnknown:
      return out << "unknown";
    case ExternalNetworkMode::kTap:
      return out << "tap";
    case ExternalNetworkMode::kSlirp:
      return out << "slirp";
  }
}
Result<ExternalNetworkMode> ParseExternalNetworkMode(std::string_view str) {
  if (absl::EqualsIgnoreCase(str, "tap")) {
    return ExternalNetworkMode::kTap;
  } else if (absl::EqualsIgnoreCase(str, "slirp")) {
    return ExternalNetworkMode::kSlirp;
  } else {
    return CF_ERRF(
        "\"{}\" is not a valid ExternalNetworkMode. Valid values are \"tap\" "
        "and \"slirp\"",
        str);
  }
}

}  // namespace cuttlefish
