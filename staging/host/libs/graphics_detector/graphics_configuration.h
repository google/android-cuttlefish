/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "common/libs/utils/result.h"
#include "host/libs/graphics_detector/graphics_detector.h"

namespace cuttlefish {

enum class RenderingMode {
  kNone,
  kGuestSwiftShader,
  kGfxstream,
  kGfxstreamGuestAngle,
  kGfxstreamGuestAngleHostSwiftshader,
  kVirglRenderer,
};
Result<RenderingMode> GetRenderingMode(const std::string& mode);

struct AngleFeatureOverrides {
  std::string angle_feature_overrides_enabled;
  std::string angle_feature_overrides_disabled;
};
Result<AngleFeatureOverrides> GetNeededAngleFeatures(
    RenderingMode mode, const GraphicsAvailability& availability);

struct VhostUserGpuHostRendererFeatures {
  // If true, host Virtio GPU blob resources will be allocated with
  // external memory and exported file descriptors will be shared
  // with the VMM for mapping resources into the guest address space.
  bool external_blob = false;

  // If true, host Virtio GPU blob resources will be allocated with
  // shmem and exported file descriptors will be shared with the VMM
  // for mapping resources into the guest address space.
  //
  // This is an extension of the above external_blob that allows the
  // VMM to map resources without graphics API support but requires
  // additional features (VK_EXT_external_memory_host) from the GPU
  // driver and is potentially less performant.
  bool system_blob = false;
};
Result<VhostUserGpuHostRendererFeatures>
GetNeededVhostUserGpuHostRendererFeatures(
    RenderingMode mode, const GraphicsAvailability& availability);

}  // namespace cuttlefish