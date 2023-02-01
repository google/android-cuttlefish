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

#include "host/libs/graphics_detector/graphics_detector_vk.h"

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "host/libs/graphics_detector/subprocess.h"
#include "host/libs/graphics_detector/vk.h"

namespace cuttlefish {
namespace {

vk::Result PopulateVulkanAvailabilityImpl(GraphicsAvailability* availability) {
  auto vk = Vk::Load();
  if (!vk) {
    LOG(DEBUG) << "Failed to Vulkan library.";
    return vk::Result::eErrorInitializationFailed;
  }
  LOG(DEBUG) << "Loaded Vulkan library.";
  availability->has_vulkan = true;

  const auto physical_devices =
      VK_EXPECT_RESULT(vk::raii::PhysicalDevices::create(vk->vk_instance));
  for (const auto& physical_device : physical_devices) {
    const auto props = physical_device.getProperties();
    if (props.deviceType != vk::PhysicalDeviceType::eDiscreteGpu) {
      continue;
    }

    const auto exts = physical_device.enumerateDeviceExtensionProperties();

    std::vector<std::string> exts_strs;
    for (const auto& ext : exts) {
      exts_strs.push_back(std::string(ext.extensionName));
    }

    availability->has_discrete_gpu = true;
    availability->discrete_gpu_device_name = std::string(props.deviceName);
    availability->discrete_gpu_device_extensions =
        android::base::Join(exts_strs, ' ');
    break;
  }

  return vk::Result::eSuccess;
}

}  // namespace

void PopulateVulkanAvailability(GraphicsAvailability* availability) {
  DoWithSubprocessCheck("PopulateVulkanAvailability", [&]() {
    PopulateVulkanAvailabilityImpl(availability);
  });
}

}  // namespace cuttlefish
