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

#include "cuttlefish/host/commands/assemble_cvd/guest_config.h"
#include "cuttlefish/host/graphics_detector/graphics_detector.pb.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/gpu_mode.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

gfxstream::proto::GraphicsAvailability
GetGraphicsAvailabilityWithSubprocessCheck();

Result<GpuMode> ConfigureGpuSettings(
    const gfxstream::proto::GraphicsAvailability& graphics_availability,
    GpuMode gpu_mode_arg, const std::string& gpu_vhost_user_mode_arg,
    const std::string& gpu_renderer_features_arg,
    std::string& gpu_context_types_arg,
    const std::string& guest_hwui_renderer_arg,
    const std::string& guest_renderer_preload_arg, VmmMode vmm,
    const GuestConfig& guest_config,
    CuttlefishConfig::MutableInstanceSpecific& instance);

}  // namespace cuttlefish
