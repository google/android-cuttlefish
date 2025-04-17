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

#include "cuttlefish/host/graphics_detector/graphics_detector.h"

#include "cuttlefish/host/graphics_detector/graphics_detector_gl.h"
#include "cuttlefish/host/graphics_detector/graphics_detector_vk.h"
#include "cuttlefish/host/graphics_detector/graphics_detector_vk_external_memory_host.h"
#include "cuttlefish/host/graphics_detector/graphics_detector_vk_precision_qualifiers_on_yuv_samplers.h"
#include "cuttlefish/host/graphics_detector/subprocess.h"

namespace gfxstream {

::gfxstream::proto::GraphicsAvailability DetectGraphicsAvailability() {
  ::gfxstream::proto::GraphicsAvailability availability;

  using GraphicsCheckFn = gfxstream::expected<Ok, std::string>(
      ::gfxstream::proto::GraphicsAvailability*);
  const std::vector<std::pair<std::string, GraphicsCheckFn*>> checks = {
      {"PopulateEglAndGlesAvailability", PopulateEglAndGlesAvailability},
      {"PopulateVulkanAvailability", PopulateVulkanAvailability},
      {"PopulateVulkanExternalMemoryHostQuirk",
       PopulateVulkanExternalMemoryHostQuirk},
      {"PopulateVulkanPrecisionQualifiersOnYuvSamplersQuirk",
       PopulateVulkanPrecisionQualifiersOnYuvSamplersQuirk},
  };

  for (const auto& check : checks) {
    auto result =
        DoWithSubprocessCheck([&]() { return check.second(&availability); });
    if (!result.ok()) {
      availability.add_errors("Graphics check failure for " + check.first +
                              ": " + result.error());
    }
  }

  return availability;
}

}  // namespace gfxstream
