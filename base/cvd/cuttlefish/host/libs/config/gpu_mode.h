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

#pragma once

#include <string>
#include <string_view>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

enum class GpuMode {
  Auto,
  Custom,
  DrmVirgl,
  Gfxstream,
  GfxstreamGuestAngle,
  GfxstreamGuestAngleHostLavapipe,
  GfxstreamGuestAngleHostSwiftshader,
  GuestSwiftshader,
  None,
};

inline constexpr std::string_view kGpuModeAuto = "auto";
inline constexpr std::string_view kGpuModeCustom = "custom";
inline constexpr std::string_view kGpuModeDrmVirgl = "drm_virgl";
inline constexpr std::string_view kGpuModeGfxstream = "gfxstream";
inline constexpr std::string_view kGpuModeGfxstreamGuestAngle =
    "gfxstream_guest_angle";
inline constexpr std::string_view kGpuModeGfxstreamGuestAngleHostLavapipe =
    "gfxstream_guest_angle_host_lavapipe";
inline constexpr std::string_view kGpuModeGfxstreamGuestAngleHostSwiftshader =
    "gfxstream_guest_angle_host_swiftshader";
inline constexpr std::string_view kGpuModeGuestSwiftshader =
    "guest_swiftshader";
inline constexpr std::string_view kGpuModeNone = "none";

template <typename Sink>
void AbslStringify(Sink& sink, GpuMode mode) {
  switch (mode) {
    case GpuMode::Auto:
      sink.Append(kGpuModeAuto);
      break;
    case GpuMode::Custom:
      sink.Append(kGpuModeCustom);
      break;
    case GpuMode::DrmVirgl:
      sink.Append(kGpuModeDrmVirgl);
      break;
    case GpuMode::Gfxstream:
      sink.Append(kGpuModeGfxstream);
      break;
    case GpuMode::GfxstreamGuestAngle:
      sink.Append(kGpuModeGfxstreamGuestAngle);
      break;
    case GpuMode::GfxstreamGuestAngleHostLavapipe:
      sink.Append(kGpuModeGfxstreamGuestAngleHostLavapipe);
      break;
    case GpuMode::GfxstreamGuestAngleHostSwiftshader:
      sink.Append(kGpuModeGfxstreamGuestAngleHostSwiftshader);
      break;
    case GpuMode::GuestSwiftshader:
      sink.Append(kGpuModeGuestSwiftshader);
      break;
    case GpuMode::None:
      sink.Append(kGpuModeNone);
      break;
  }
}

Result<GpuMode> GpuModeFromString(std::string_view mode);
std::string GpuModeString(GpuMode mode);
std::string_view format_as(GpuMode mode);  // For libfmt

bool IsGfxstreamMode(GpuMode mode);
bool IsGfxstreamGuestAngleMode(GpuMode mode);

}  // namespace cuttlefish
