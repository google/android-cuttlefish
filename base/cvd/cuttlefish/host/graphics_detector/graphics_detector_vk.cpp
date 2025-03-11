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

#include "cuttlefish/host/graphics_detector/graphics_detector_vk.h"

#include "cuttlefish/host/graphics_detector/vulkan.h"

namespace gfxstream {
namespace {

gfxstream::expected<Ok, vk::Result> PopulateVulkanAvailabilityImpl(
    ::gfxstream::proto::GraphicsAvailability* availability) {
  auto vk = GFXSTREAM_EXPECT(Vk::Load());

  ::gfxstream::proto::VulkanAvailability* vulkanAvailability =
      availability->mutable_vulkan();

  const auto physicalDevices =
      VK_EXPECT_RV(vk.instance().enumeratePhysicalDevices());
  for (const auto& physicalDevice : physicalDevices) {
    auto* outPhysicalDevice = vulkanAvailability->add_physical_devices();

    const auto props = physicalDevice.getProperties();
    outPhysicalDevice->set_name(std::string(props.deviceName));
    outPhysicalDevice->set_type(
        props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu
            ? ::gfxstream::proto::VulkanPhysicalDevice::TYPE_DISCRETE_GPU
            : ::gfxstream::proto::VulkanPhysicalDevice::TYPE_OTHER);

    const auto exts =
        VK_EXPECT_RV(physicalDevice.enumerateDeviceExtensionProperties());
    for (const auto& ext : exts) {
      outPhysicalDevice->add_extensions(std::string(ext.extensionName));
    }
  }

  return Ok{};
}

}  // namespace

gfxstream::expected<Ok, std::string> PopulateVulkanAvailability(
    ::gfxstream::proto::GraphicsAvailability* availability) {
  return PopulateVulkanAvailabilityImpl(availability)
      .transform_error([](vk::Result r) { return vk::to_string(r); });
}

}  // namespace gfxstream
