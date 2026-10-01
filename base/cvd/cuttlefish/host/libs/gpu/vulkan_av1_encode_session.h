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

#include <memory>
#include <vector>

#include "vulkan/vulkan_core.h"

#include "cuttlefish/host/libs/gpu/rgba_to_nv12.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_encode_settings.h"
#include "cuttlefish/host/libs/gpu/vulkan_av1_session_setup.h"
#include "cuttlefish/host/libs/gpu/vulkan_video_context.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// One encoded frame, with the sequence header in front of a key frame.
struct VulkanAv1EncodedFrame {
  std::vector<uint8_t> bitstream;
};

// Where the driver placed a frame's bitstream in the output buffer, as the
// encode feedback query reports it.
struct VulkanBitstreamRange {
  uint32_t offset = 0;
  uint32_t size = 0;
};

// Hardware AV1 encoder built on VK_KHR_video_encode_av1.
//
// Takes packed RGBA frames and converts them to NV12 on the host. The frame
// reaches the encode source image through a copy on the compute queue, since a
// video encode queue need not accept copy commands. Encodes every frame as a
// key frame. Used from one thread.
class VulkanAv1EncodeSession {
 public:
  static Result<std::unique_ptr<VulkanAv1EncodeSession>> Create(
      const VulkanAv1SessionConfig& config);

  ~VulkanAv1EncodeSession();

  VulkanAv1EncodeSession(const VulkanAv1EncodeSession&) = delete;
  VulkanAv1EncodeSession& operator=(const VulkanAv1EncodeSession&) = delete;

  // The coded extent in use, which is the frame size rounded up to the
  // driver's alignment.
  VkExtent2D coded_extent() const { return settings_.coded_extent; }

  // Encodes one frame as a key frame.
  Result<VulkanAv1EncodedFrame> EncodeFrame(const uint8_t* pixels,
                                            const Nv12ConversionParams& params);

 private:
  VulkanAv1EncodeSession(std::shared_ptr<VulkanVideoContext> context,
                         const VulkanAv1EncodeSettings& settings,
                         VulkanAv1SessionResources resources);

  Result<VkSemaphore> Upload(const uint8_t* pixels,
                             const Nv12ConversionParams& params);
  Result<void> FlushStaging();
  // Copies the converted frame from the staging buffer into the encode source
  // image on the compute queue, signalling the copy semaphore.
  Result<void> SubmitStagingCopy();
  Result<void> SubmitAndWait(VkSemaphore wait_semaphore);
  Result<VulkanBitstreamRange> ReadBitstreamRange();

  std::shared_ptr<VulkanVideoContext> context_;
  VulkanAv1EncodeSettings settings_;
  VulkanAv1SessionResources resources_;
  uint64_t frame_count_ = 0;
};

}  // namespace cuttlefish
