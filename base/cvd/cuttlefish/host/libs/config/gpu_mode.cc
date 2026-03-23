/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/gpu_mode.h"

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"

#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<GpuMode> GpuModeFromString(std::string_view mode) {
  if (mode == kGpuModeAuto) {
    return GpuMode::Auto;
  } else if (mode == kGpuModeCustom) {
    return GpuMode::Custom;
  } else if (mode == kGpuModeDrmVirgl) {
    return GpuMode::DrmVirgl;
  } else if (mode == kGpuModeGfxstream) {
    return GpuMode::Gfxstream;
  } else if (mode == kGpuModeGfxstreamGuestAngle) {
    return GpuMode::GfxstreamGuestAngle;
  } else if (mode == kGpuModeGfxstreamGuestAngleHostSwiftshader) {
    return GpuMode::GfxstreamGuestAngleHostSwiftshader;
  } else if (mode == kGpuModeGfxstreamGuestAngleHostLavapipe) {
    return GpuMode::GfxstreamGuestAngleHostLavapipe;
  } else if (mode == kGpuModeGuestSwiftshader) {
    return GpuMode::GuestSwiftshader;
  } else if (mode == kGpuModeNone) {
    return GpuMode::None;
  } else {
    return CF_ERRF("Invalid gpu_mode provided: ", mode);
  }
}

std::string GpuModeString(GpuMode mode) { return absl::StrCat(mode); }

std::string_view format_as(GpuMode mode) {
  switch (mode) {
    case GpuMode::Auto:
      return kGpuModeAuto;
    case GpuMode::Custom:
      return kGpuModeCustom;
      break;
    case GpuMode::DrmVirgl:
      return kGpuModeDrmVirgl;
      break;
    case GpuMode::Gfxstream:
      return kGpuModeGfxstream;
      break;
    case GpuMode::GfxstreamGuestAngle:
      return kGpuModeGfxstreamGuestAngle;
      break;
    case GpuMode::GfxstreamGuestAngleHostLavapipe:
      return kGpuModeGfxstreamGuestAngleHostLavapipe;
      break;
    case GpuMode::GfxstreamGuestAngleHostSwiftshader:
      return kGpuModeGfxstreamGuestAngleHostSwiftshader;
      break;
    case GpuMode::GuestSwiftshader:
      return kGpuModeGuestSwiftshader;
      break;
    case GpuMode::None:
      return kGpuModeNone;
  }
}

bool IsGfxstreamMode(GpuMode mode) {
  return mode == GpuMode::Gfxstream || mode == GpuMode::GfxstreamGuestAngle ||
         mode == GpuMode::GfxstreamGuestAngleHostLavapipe ||
         mode == GpuMode::GfxstreamGuestAngleHostSwiftshader;
}

bool IsGfxstreamGuestAngleMode(GpuMode mode) {
  return mode == GpuMode::GfxstreamGuestAngle ||
         mode == GpuMode::GfxstreamGuestAngleHostLavapipe ||
         mode == GpuMode::GfxstreamGuestAngleHostSwiftshader;
}

}  // namespace cuttlefish
