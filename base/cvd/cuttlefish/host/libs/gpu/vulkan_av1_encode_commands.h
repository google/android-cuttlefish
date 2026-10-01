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

#include <stdint.h>

#include "cuttlefish/host/libs/gpu/vulkan_av1_dpb.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_settings.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_session_setup.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// What a recorded frame changes about the session once the device has
// accepted it.
struct VulkanFrameCommit {
  bool key_frame = false;
  uint8_t order_hint = 0;
};

// What the encode command buffer of one frame is recorded from.
struct VulkanAv1FrameCommands {
  VulkanFrameCommit commit;
  bool first_frame = false;
};

// Records the encode command buffer of one frame: the input barriers, the
// coding scope with its control commands, and the encode with its feedback
// query.
Result<void> RecordVulkanAv1EncodeCommands(
    const VulkanVideoContext& context,
    const VulkanAv1SessionResources& resources,
    const VulkanAv1EncodeSettings& settings, const Av1DpbPingPong& dpb,
    const VulkanAv1FrameCommands& frame);

// Records the copy of an NV12 frame from the staging buffer into the encode
// input image, on the compute queue family's command buffer.
Result<void> RecordVulkanAv1StagingCopy(
    const VulkanVideoContext& context,
    const VulkanAv1SessionResources& resources,
    const VulkanAv1EncodeSettings& settings);

}  // namespace cuttlefish
