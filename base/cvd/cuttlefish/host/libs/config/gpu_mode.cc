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

#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

constexpr std::string_view kAuto = "auto";
constexpr std::string_view kCustom = "custom";
constexpr std::string_view kDrmVirgl = "drm_virgl";
constexpr std::string_view kGfxstream = "gfxstream";
constexpr std::string_view kGfxstreamGuestAngle = "gfxstream_guest_angle";
constexpr std::string_view kGfxstreamGuestAngleHostSwiftshader =
    "gfxstream_guest_angle_host_swiftshader";
constexpr std::string_view kGfxstreamGuestAngleHostLavapipe =
    "gfxstream_guest_angle_host_lavapipe";
constexpr std::string_view kGuestSwiftshader = "guest_swiftshader";
constexpr std::string_view kNone = "none";

}  // namespace

Result<GpuMode> GpuModeFromString(std::string_view mode) {
  if (mode == kAuto) {
    return GpuMode::Auto;
  } else if (mode == kCustom) {
    return GpuMode::Custom;
  } else if (mode == kDrmVirgl) {
    return GpuMode::DrmVirgl;
  } else if (mode == kGfxstream) {
    return GpuMode::Gfxstream;
  } else if (mode == kGfxstreamGuestAngle) {
    return GpuMode::GfxstreamGuestAngle;
  } else if (mode == kGfxstreamGuestAngleHostSwiftshader) {
    return GpuMode::GfxstreamGuestAngleHostSwiftshader;
  } else if (mode == kGfxstreamGuestAngleHostLavapipe) {
    return GpuMode::GfxstreamGuestAngleHostLavapipe;
  } else if (mode == kGuestSwiftshader) {
    return GpuMode::GuestSwiftshader;
  } else if (mode == kNone) {
    return GpuMode::None;
  } else {
    return CF_ERRF("Invalid gpu_mode provided: ", mode);
  }
}

std::string GpuModeString(GpuMode mode) {
  switch (mode) {
    case GpuMode::Auto:
      return std::string(kAuto);
    case GpuMode::Custom:
      return std::string(kCustom);
    case GpuMode::DrmVirgl:
      return std::string(kDrmVirgl);
    case GpuMode::Gfxstream:
      return std::string(kGfxstream);
    case GpuMode::GfxstreamGuestAngle:
      return std::string(kGfxstreamGuestAngle);
    case GpuMode::GfxstreamGuestAngleHostLavapipe:
      return std::string(kGfxstreamGuestAngleHostLavapipe);
    case GpuMode::GfxstreamGuestAngleHostSwiftshader:
      return std::string(kGfxstreamGuestAngleHostSwiftshader);
    case GpuMode::GuestSwiftshader:
      return std::string(kGuestSwiftshader);
    case GpuMode::None:
      return std::string(kNone);
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
