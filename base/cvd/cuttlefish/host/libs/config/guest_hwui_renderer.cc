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

#include "cuttlefish/host/libs/config/guest_hwui_renderer.h"

#include <ostream>
#include <string>
#include <string_view>

#include "absl/strings/match.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

std::ostream& operator<<(std::ostream& out, GuestHwuiRenderer renderer) {
  return out << ToString(renderer);
}

std::string ToString(GuestHwuiRenderer renderer) {
  switch (renderer) {
    case GuestHwuiRenderer::kUnknown:
      return "unknown";
    case GuestHwuiRenderer::kSkiaGl:
      return "skiagl";
    case GuestHwuiRenderer::kSkiaVk:
      return "skiavk";
  }
}

Result<GuestHwuiRenderer> ParseGuestHwuiRenderer(std::string_view str) {
  if (absl::EqualsIgnoreCase(str, "unknown")) {
    return GuestHwuiRenderer::kUnknown;
  } else if (absl::EqualsIgnoreCase(str, "skiagl")) {
    return GuestHwuiRenderer::kSkiaGl;
  } else if (absl::EqualsIgnoreCase(str, "skiavk")) {
    return GuestHwuiRenderer::kSkiaVk;
  } else {
    return CF_ERRF("\"{}\" is not a valid HWUI renderer.", str);
  }
}

}  // namespace cuttlefish
