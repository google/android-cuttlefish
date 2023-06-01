/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "host/libs/graphics_detector/graphics_detector.h"

#include <sstream>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "host/libs/graphics_detector/graphics_detector_gl.h"
#include "host/libs/graphics_detector/graphics_detector_vk.h"
#include "host/libs/graphics_detector/graphics_detector_vk_precision_qualifiers_on_yuv_samplers.h"

namespace cuttlefish {
namespace {

std::string ToLower(const std::string& v) {
  std::string result = v;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

bool IsLikelySoftwareRenderer(const std::string& renderer) {
  const std::string lower_renderer = ToLower(renderer);
  return lower_renderer.find("llvmpipe") != std::string::npos;
}

}  // namespace

bool ShouldEnableAcceleratedRendering(
    const GraphicsAvailability& availability) {
  const bool sufficient_gles2 =
      availability.can_init_gles2_on_egl_surfaceless &&
      !IsLikelySoftwareRenderer(availability.gles2_renderer);
  const bool sufficient_gles3 =
      availability.can_init_gles3_on_egl_surfaceless &&
      !IsLikelySoftwareRenderer(availability.gles3_renderer);
  return (sufficient_gles2 || sufficient_gles3) &&
         availability.has_discrete_gpu;
}

// Runs various graphics tests inside of subprocesses first to ensure that
// this function can complete successfully without crashing the Cuttlefish
// launcher. Configurations such as GCE instances without a GPU but with GPU
// drivers for example have seen crashes.
GraphicsAvailability GetGraphicsAvailabilityWithSubprocessCheck() {
  GraphicsAvailability availability;
  PopulateEglAndGlesAvailability(&availability);
  PopulateVulkanAvailability(&availability);
  PopulateVulkanPrecisionQualifiersOnYuvSamplersQuirk(&availability);
  return availability;
}

std::ostream& operator<<(std::ostream& stream,
                         const GraphicsAvailability& availability) {
  std::ios_base::fmtflags flags_backup(stream.flags());
  stream << std::boolalpha;
  stream << "Graphics Availability:\n";

  stream << "\n";
  stream << "EGL available: " << availability.has_egl << "\n";
  stream << "Vulkan lib available: " << availability.has_vulkan << "\n";

  stream << "\n";
  stream << "EGL client extensions: " << availability.egl_client_extensions
         << "\n";

  stream << "\n";
  stream << "EGL display vendor: " << availability.egl_vendor << "\n";
  stream << "EGL display version: " << availability.egl_version << "\n";
  stream << "EGL display extensions: " << availability.egl_extensions << "\n";

  stream << "GLES2 can init on surfaceless display: "
         << availability.can_init_gles2_on_egl_surfaceless << "\n";
  stream << "GLES2 vendor: " << availability.gles2_vendor << "\n";
  stream << "GLES2 version: " << availability.gles2_version << "\n";
  stream << "GLES2 renderer: " << availability.gles2_renderer << "\n";
  stream << "GLES2 extensions: " << availability.gles2_extensions << "\n";

  stream << "GLES3 can init on surfaceless display: "
         << availability.can_init_gles3_on_egl_surfaceless << "\n";
  stream << "GLES3 vendor: " << availability.gles3_vendor << "\n";
  stream << "GLES3 version: " << availability.gles3_version << "\n";
  stream << "GLES3 renderer: " << availability.gles3_renderer << "\n";
  stream << "GLES3 extensions: " << availability.gles3_extensions << "\n";

  stream << "\n";
  stream << "Vulkan discrete GPU detected: " << availability.has_discrete_gpu
         << "\n";
  if (availability.has_discrete_gpu) {
    stream << "Vulkan discrete GPU device name: "
           << availability.discrete_gpu_device_name << "\n";
    stream << "Vulkan discrete GPU device extensions: "
           << availability.discrete_gpu_device_extensions << "\n";
  }

  stream
      << "Vulkan has quirk with precision qualifiers on YUV samplers: "
      << availability.vulkan_has_issue_with_precision_qualifiers_on_yuv_samplers
      << "\n";

  stream << "\n";
  stream << "Accelerated rendering supported: "
         << ShouldEnableAcceleratedRendering(availability);

  stream.flags(flags_backup);
  return stream;
}

} // namespace cuttlefish
