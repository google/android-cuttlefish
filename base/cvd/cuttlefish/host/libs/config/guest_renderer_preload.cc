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

#include "cuttlefish/host/libs/config/guest_renderer_preload.h"

#include <ostream>
#include <string>
#include <string_view>

#include "absl/strings/match.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

std::ostream& operator<<(std::ostream& out, GuestRendererPreload preload) {
  return out << ToString(preload);
}

std::string ToString(GuestRendererPreload preload) {
  switch (preload) {
    case GuestRendererPreload::kAuto:
      return "auto";
    case GuestRendererPreload::kGuestDefault:
      return "default";
    case GuestRendererPreload::kEnabled:
      return "enabled";
    case GuestRendererPreload::kDisabled:
      return "disabled";
  }
}

Result<GuestRendererPreload> ParseGuestRendererPreload(std::string_view str) {
  if (absl::EqualsIgnoreCase(str, "auto")) {
    return GuestRendererPreload::kAuto;
  } else if (absl::EqualsIgnoreCase(str, "default")) {
    return GuestRendererPreload::kGuestDefault;
  } else if (absl::EqualsIgnoreCase(str, "enabled")) {
    return GuestRendererPreload::kEnabled;
  } else if (absl::EqualsIgnoreCase(str, "disabled")) {
    return GuestRendererPreload::kDisabled;
  } else {
    return CF_ERRF("\"{}\" is not a valid renderer preload.", str);
  }
}

}  // namespace cuttlefish
