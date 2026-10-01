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

#ifdef __APPLE__

#include "cuttlefish/host/graphics_detector/graphics_detector.pb.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"

#else

#include <string>

#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"
#include "cuttlefish/host/graphics_detector/graphics_detector.pb.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"
#include "cuttlefish/result/result.h"

#endif

namespace cuttlefish {

gfxstream::proto::GraphicsAvailability
GetGraphicsAvailabilityWithSubprocessCheck();

#ifdef __APPLE__

Result<GpuMode> SelectGpuMode(GpuMode given_gpu_mode);

#else

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

Result<GpuMode> SelectGpuMode(
    GpuMode given_gpu_mode, VmmMode vmm, const GuestConfig& guest_config,
    const std::string& gpu_context_types,
    const gfxstream::proto::GraphicsAvailability& graphics_availability);

Result<bool> SelectGpuVhostUserMode(GpuMode gpu_mode,
                                    const std::string& gpu_vhost_user_mode_arg,
                                    VmmMode vmm);

Result<VhostUserGpuHostRendererFeatures>
GetNeededVhostUserGpuHostRendererFeatures(
    GpuMode mode, const ::gfxstream::proto::GraphicsAvailability& availability);

struct AngleFeatureOverrides {
  std::string angle_feature_overrides_enabled;
  std::string angle_feature_overrides_disabled;
};

Result<AngleFeatureOverrides> GetNeededAngleFeatures(
    GpuMode mode, const ::gfxstream::proto::GraphicsAvailability& availability);

Result<void> SelectGpuSettings(
    const gfxstream::proto::GraphicsAvailability& graphics_availability,
    GpuMode gpu_mode, const std::string& gpu_renderer_features_arg,
    const std::string& guest_hwui_renderer_arg,
    const std::string& guest_renderer_preload_arg,
    const GuestConfig& guest_config,
    CuttlefishConfig::MutableInstanceSpecific& instance);

#endif

}  // namespace cuttlefish
