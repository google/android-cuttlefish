/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "cuttlefish/host/graphics_detector/vulkan.h"

#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace gfxstream {
namespace {

constexpr const bool kEnableValidationLayers = false;

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*) {
  if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
    std::cout << pCallbackData->pMessage << std::endl;
  } else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
    std::cout << pCallbackData->pMessage << std::endl;
  } else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
    std::cerr << pCallbackData->pMessage << std::endl;
  } else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
    std::cerr << pCallbackData->pMessage << std::endl;
  }
  return VK_FALSE;
}

uint32_t GetMemoryType(const vk::PhysicalDevice& physical_device,
                       uint32_t memory_type_mask,
                       vk::MemoryPropertyFlags memoryProperties) {
  const auto props = physical_device.getMemoryProperties();
  for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
    if (!(memory_type_mask & (1 << i))) {
      continue;
    }
    if ((props.memoryTypes[i].propertyFlags & memoryProperties) !=
        memoryProperties) {
      continue;
    }
    return i;
  }
  return -1;
}

}  // namespace

gfxstream::expected<Vk::BufferWithMemory, vk::Result> DoCreateBuffer(
    const vk::PhysicalDevice& physical_device, const vk::UniqueDevice& device,
    vk::DeviceSize buffer_size, vk::BufferUsageFlags buffer_usages,
    vk::MemoryPropertyFlags bufferMemoryProperties) {
  const vk::BufferCreateInfo bufferCreateInfo = {
      .size = static_cast<VkDeviceSize>(buffer_size),
      .usage = buffer_usages,
      .sharingMode = vk::SharingMode::eExclusive,
  };
  auto buffer = VK_EXPECT_RV(device->createBufferUnique(bufferCreateInfo));

  vk::MemoryRequirements bufferMemoryRequirements{};
  device->getBufferMemoryRequirements(*buffer, &bufferMemoryRequirements);

  const auto bufferMemoryType =
      GetMemoryType(physical_device, bufferMemoryRequirements.memoryTypeBits,
                    bufferMemoryProperties);

  const vk::MemoryAllocateInfo bufferMemoryAllocateInfo = {
      .allocationSize = bufferMemoryRequirements.size,
      .memoryTypeIndex = bufferMemoryType,
  };
  auto bufferMemory =
      VK_EXPECT_RV(device->allocateMemoryUnique(bufferMemoryAllocateInfo));

  VK_EXPECT_RESULT(device->bindBufferMemory(*buffer, *bufferMemory, 0));

  return Vk::BufferWithMemory{
      .buffer = std::move(buffer),
      .bufferMemory = std::move(bufferMemory),
  };
}

/*static*/
gfxstream::expected<Vk, vk::Result> Vk::Load(
    const std::vector<std::string>& requestedInstanceExtensions,
    const std::vector<std::string>& requestedInstanceLayers,
    const std::vector<std::string>& requestedDeviceExtensions) {
  vk::detail::DynamicLoader loader;

  VULKAN_HPP_DEFAULT_DISPATCHER.init(
      loader.getProcAddress<PFN_vkGetInstanceProcAddr>(
          "vkGetInstanceProcAddr"));

  std::vector<const char*> requestedInstanceExtensionsChars;
  requestedInstanceExtensionsChars.reserve(requestedInstanceExtensions.size());
  for (const auto& e : requestedInstanceExtensions) {
    requestedInstanceExtensionsChars.push_back(e.c_str());
  }
  if (kEnableValidationLayers) {
    requestedInstanceExtensionsChars.push_back(
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  std::vector<const char*> requestedInstanceLayersChars;
  requestedInstanceLayersChars.reserve(requestedInstanceLayers.size());
  for (const auto& l : requestedInstanceLayers) {
    requestedInstanceLayersChars.push_back(l.c_str());
  }

  const vk::ApplicationInfo applicationInfo = {
      .pApplicationName = "Cuttlefish Graphics Detector",
      .applicationVersion = 1,
      .pEngineName = "Cuttlefish Graphics Detector",
      .engineVersion = 1,
      .apiVersion = VK_API_VERSION_1_2,
  };
  const vk::InstanceCreateInfo instanceCreateInfo = {
      .pApplicationInfo = &applicationInfo,
      .enabledLayerCount =
          static_cast<uint32_t>(requestedInstanceLayersChars.size()),
      .ppEnabledLayerNames = requestedInstanceLayersChars.data(),
      .enabledExtensionCount =
          static_cast<uint32_t>(requestedInstanceExtensionsChars.size()),
      .ppEnabledExtensionNames = requestedInstanceExtensionsChars.data(),
  };

  auto instance = VK_EXPECT_RV(vk::createInstanceUnique(instanceCreateInfo));

  VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

  std::optional<vk::UniqueDebugUtilsMessengerEXT> debugMessenger;
  if (kEnableValidationLayers) {
    const vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = VulkanDebugCallback,
        .pUserData = nullptr,
    };
    debugMessenger = VK_EXPECT_RV(
        instance->createDebugUtilsMessengerEXTUnique(debugCreateInfo));
  }

  const auto physicalDevices =
      VK_EXPECT_RV(instance->enumeratePhysicalDevices());
  vk::PhysicalDevice physicalDevice = std::move(physicalDevices[0]);

  std::unordered_set<std::string> availableDeviceExtensions;
  {
    const auto exts =
        VK_EXPECT_RV(physicalDevice.enumerateDeviceExtensionProperties());
    for (const auto& ext : exts) {
      availableDeviceExtensions.emplace(ext.extensionName);
    }
  }

  const auto features2 =
      physicalDevice
          .getFeatures2<vk::PhysicalDeviceFeatures2,  //
                        vk::PhysicalDeviceSamplerYcbcrConversionFeatures>();

  bool ycbcr_conversion_needed = false;

  std::vector<const char*> requestedDeviceExtensionsChars;
  requestedDeviceExtensionsChars.reserve(requestedDeviceExtensions.size());
  for (const auto& e : requestedDeviceExtensions) {
    if (e == std::string(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME)) {
      // The interface of VK_KHR_sampler_ycbcr_conversion was promoted to core
      // in Vulkan 1.1 but the feature/functionality is still optional. Check
      // here:
      const auto& sampler_features =
          features2.get<vk::PhysicalDeviceSamplerYcbcrConversionFeatures>();

      if (sampler_features.samplerYcbcrConversion == VK_FALSE) {
        return gfxstream::unexpected(vk::Result::eErrorExtensionNotPresent);
      }
      ycbcr_conversion_needed = true;
    } else {
      if (availableDeviceExtensions.find(e) ==
          availableDeviceExtensions.end()) {
        return gfxstream::unexpected(vk::Result::eErrorExtensionNotPresent);
      }
      requestedDeviceExtensionsChars.push_back(e.c_str());
    }
  }

  uint32_t queueFamilyIndex = -1;
  {
    const auto props = physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < props.size(); i++) {
      const auto& prop = props[i];
      if (prop.queueFlags & vk::QueueFlagBits::eGraphics) {
        queueFamilyIndex = i;
        break;
      }
    }
  }

  const float queue_priority = 1.0f;
  const vk::DeviceQueueCreateInfo device_queue_create_info = {
      .queueFamilyIndex = queueFamilyIndex,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
  };
  const vk::PhysicalDeviceVulkan11Features device_enable_features = {
      .samplerYcbcrConversion = ycbcr_conversion_needed,
  };
  const vk::DeviceCreateInfo deviceCreateInfo = {
      .pNext = &device_enable_features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &device_queue_create_info,
      .enabledLayerCount =
          static_cast<uint32_t>(requestedInstanceLayersChars.size()),
      .ppEnabledLayerNames = requestedInstanceLayersChars.data(),
      .enabledExtensionCount =
          static_cast<uint32_t>(requestedDeviceExtensionsChars.size()),
      .ppEnabledExtensionNames = requestedDeviceExtensionsChars.data(),
  };
  auto device =
      VK_EXPECT_RV(physicalDevice.createDeviceUnique(deviceCreateInfo));
  auto queue = device->getQueue(queueFamilyIndex, 0);

  const vk::CommandPoolCreateInfo commandPoolCreateInfo = {
      .queueFamilyIndex = queueFamilyIndex,
  };
  auto commandPool =
      VK_EXPECT_RV(device->createCommandPoolUnique(commandPoolCreateInfo));

  auto stagingBuffer =
      VK_EXPECT(DoCreateBuffer(physicalDevice, device, kStagingBufferSize,
                               vk::BufferUsageFlagBits::eTransferDst |
                                   vk::BufferUsageFlagBits::eTransferSrc,
                               vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent));

  return Vk(std::move(loader), std::move(instance), std::move(debugMessenger),
            std::move(physicalDevice), std::move(device), std::move(queue),
            queueFamilyIndex, std::move(commandPool),
            std::move(stagingBuffer.buffer),
            std::move(stagingBuffer.bufferMemory));
}

gfxstream::expected<Vk::BufferWithMemory, vk::Result> Vk::CreateBuffer(
    vk::DeviceSize bufferSize, vk::BufferUsageFlags bufferUsages,
    vk::MemoryPropertyFlags bufferMemoryProperties) {
  return DoCreateBuffer(mPhysicalDevice, mDevice, bufferSize, bufferUsages,
                        bufferMemoryProperties);
}

gfxstream::expected<Vk::BufferWithMemory, vk::Result> Vk::CreateBufferWithData(
    vk::DeviceSize bufferSize, vk::BufferUsageFlags bufferUsages,
    vk::MemoryPropertyFlags bufferMemoryProperties,
    const uint8_t* buffer_data) {
  auto buffer = VK_EXPECT(CreateBuffer(
      bufferSize, bufferUsages | vk::BufferUsageFlagBits::eTransferDst,
      bufferMemoryProperties));

  void* mapped = VK_EXPECT_RV(
      mDevice->mapMemory(*mStagingBufferMemory, 0, kStagingBufferSize));

  std::memcpy(mapped, buffer_data, bufferSize);

  mDevice->unmapMemory(*mStagingBufferMemory);

  DoCommandsImmediate([&](vk::UniqueCommandBuffer& cmd) {
    const std::vector<vk::BufferCopy> regions = {
        vk::BufferCopy{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = bufferSize,
        },
    };
    cmd->copyBuffer(*mStagingBuffer, *buffer.buffer, regions);
    return vk::Result::eSuccess;
  });

  return std::move(buffer);
}

gfxstream::expected<Vk::ImageWithMemory, vk::Result> Vk::CreateImage(
    uint32_t width, uint32_t height, vk::Format format,
    vk::ImageUsageFlags usages, vk::MemoryPropertyFlags memoryProperties,
    vk::ImageLayout returnedLayout) {
  const vk::ImageCreateInfo imageCreateInfo = {
      .imageType = vk::ImageType::e2D,
      .format = format,
      .extent =
          {
              .width = width,
              .height = height,
              .depth = 1,
          },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = vk::SampleCountFlagBits::e1,
      .tiling = vk::ImageTiling::eOptimal,
      .usage = usages,
      .sharingMode = vk::SharingMode::eExclusive,
      .initialLayout = vk::ImageLayout::eUndefined,
  };
  auto image = VK_EXPECT_RV(mDevice->createImageUnique(imageCreateInfo));

  const auto memoryRequirements = mDevice->getImageMemoryRequirements(*image);
  const uint32_t memoryIndex = GetMemoryType(
      mPhysicalDevice, memoryRequirements.memoryTypeBits, memoryProperties);

  const vk::MemoryAllocateInfo imageMemoryAllocateInfo = {
      .allocationSize = memoryRequirements.size,
      .memoryTypeIndex = memoryIndex,
  };
  auto imageMemory =
      VK_EXPECT_RV(mDevice->allocateMemoryUnique(imageMemoryAllocateInfo));

  VK_EXPECT_RESULT(mDevice->bindImageMemory(*image, *imageMemory, 0));

  const vk::ImageViewCreateInfo imageViewCreateInfo = {
      .image = *image,
      .viewType = vk::ImageViewType::e2D,
      .format = format,
      .components =
          {
              .r = vk::ComponentSwizzle::eIdentity,
              .g = vk::ComponentSwizzle::eIdentity,
              .b = vk::ComponentSwizzle::eIdentity,
              .a = vk::ComponentSwizzle::eIdentity,
          },
      .subresourceRange =
          {
              .aspectMask = vk::ImageAspectFlagBits::eColor,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };
  auto imageView =
      VK_EXPECT_RV(mDevice->createImageViewUnique(imageViewCreateInfo));

  VK_EXPECT_RESULT(DoCommandsImmediate([&](vk::UniqueCommandBuffer& cmd) {
    const std::vector<vk::ImageMemoryBarrier> imageMemoryBarriers = {
        vk::ImageMemoryBarrier{
            .srcAccessMask = {},
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = returnedLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *image,
            .subresourceRange =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        },
    };
    cmd->pipelineBarrier(
        /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
        /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
        /*dependencyFlags=*/{},
        /*memoryBarriers=*/{},
        /*bufferMemoryBarriers=*/{},
        /*imageMemoryBarriers=*/imageMemoryBarriers);

    return vk::Result::eSuccess;
  }));

  return ImageWithMemory{
      .image = std::move(image),
      .imageMemory = std::move(imageMemory),
      .imageView = std::move(imageView),
  };
}

gfxstream::expected<std::vector<uint8_t>, vk::Result> Vk::DownloadImage(
    uint32_t width, uint32_t height, const vk::UniqueImage& image,
    vk::ImageLayout currentLayout, vk::ImageLayout returnedLayout) {
  VK_EXPECT_RESULT(DoCommandsImmediate([&](vk::UniqueCommandBuffer& cmd) {
    if (currentLayout != vk::ImageLayout::eTransferSrcOptimal) {
      const std::vector<vk::ImageMemoryBarrier> imageMemoryBarriers = {
          vk::ImageMemoryBarrier{
              .srcAccessMask = vk::AccessFlagBits::eMemoryRead |
                               vk::AccessFlagBits::eMemoryWrite,
              .dstAccessMask = vk::AccessFlagBits::eTransferRead,
              .oldLayout = currentLayout,
              .newLayout = vk::ImageLayout::eTransferSrcOptimal,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vk::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },
          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }

    const std::vector<vk::BufferImageCopy> regions = {
        vk::BufferImageCopy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageOffset =
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
            .imageExtent =
                {
                    .width = width,
                    .height = height,
                    .depth = 1,
                },
        },
    };
    cmd->copyImageToBuffer(*image, vk::ImageLayout::eTransferSrcOptimal,
                           *mStagingBuffer, regions);

    if (returnedLayout != vk::ImageLayout::eTransferSrcOptimal) {
      const std::vector<vk::ImageMemoryBarrier> imageMemoryBarriers = {
          vk::ImageMemoryBarrier{
              .srcAccessMask = vk::AccessFlagBits::eTransferRead,
              .dstAccessMask = vk::AccessFlagBits::eMemoryRead |
                               vk::AccessFlagBits::eMemoryWrite,
              .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
              .newLayout = returnedLayout,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vk::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },
          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }

    return vk::Result::eSuccess;
  }));

  auto* mapped = reinterpret_cast<uint8_t*>(VK_EXPECT_RV(
      mDevice->mapMemory(*mStagingBufferMemory, 0, kStagingBufferSize)));

  std::vector<uint8_t> outPixels;
  outPixels.resize(width * height * 4);

  std::memcpy(outPixels.data(), mapped, outPixels.size());

  mDevice->unmapMemory(*mStagingBufferMemory);

  return outPixels;
}

gfxstream::expected<Vk::YuvImageWithMemory, vk::Result> Vk::CreateYuvImage(
    uint32_t width, uint32_t height, vk::ImageUsageFlags usages,
    vk::MemoryPropertyFlags memoryProperties, vk::ImageLayout layout) {
  const vk::SamplerYcbcrConversionCreateInfo conversionCreateInfo = {
      .format = vk::Format::eG8B8R83Plane420Unorm,
      .ycbcrModel = vk::SamplerYcbcrModelConversion::eYcbcr601,
      .ycbcrRange = vk::SamplerYcbcrRange::eItuNarrow,
      .components =
          {
              .r = vk::ComponentSwizzle::eIdentity,
              .g = vk::ComponentSwizzle::eIdentity,
              .b = vk::ComponentSwizzle::eIdentity,
              .a = vk::ComponentSwizzle::eIdentity,
          },
      .xChromaOffset = vk::ChromaLocation::eMidpoint,
      .yChromaOffset = vk::ChromaLocation::eMidpoint,
      .chromaFilter = vk::Filter::eLinear,
      .forceExplicitReconstruction = VK_FALSE,
  };
  auto imageSamplerConversion = VK_EXPECT_RV(
      mDevice->createSamplerYcbcrConversionUnique(conversionCreateInfo));

  const vk::SamplerYcbcrConversionInfo samplerConversionInfo = {
      .conversion = *imageSamplerConversion,
  };
  const vk::SamplerCreateInfo samplerCreateInfo = {
      .pNext = &samplerConversionInfo,
      .magFilter = vk::Filter::eLinear,
      .minFilter = vk::Filter::eLinear,
      .mipmapMode = vk::SamplerMipmapMode::eNearest,
      .addressModeU = vk::SamplerAddressMode::eClampToEdge,
      .addressModeV = vk::SamplerAddressMode::eClampToEdge,
      .addressModeW = vk::SamplerAddressMode::eClampToEdge,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1.0f,
      .compareEnable = VK_FALSE,
      .compareOp = vk::CompareOp::eLessOrEqual,
      .minLod = 0.0f,
      .maxLod = 0.25f,
      .borderColor = vk::BorderColor::eIntTransparentBlack,
      .unnormalizedCoordinates = VK_FALSE,
  };
  auto imageSampler =
      VK_EXPECT_RV(mDevice->createSamplerUnique(samplerCreateInfo));

  const vk::ImageCreateInfo imageCreateInfo = {
      .imageType = vk::ImageType::e2D,
      .format = vk::Format::eG8B8R83Plane420Unorm,
      .extent =
          {
              .width = width,
              .height = height,
              .depth = 1,
          },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = vk::SampleCountFlagBits::e1,
      .tiling = vk::ImageTiling::eOptimal,
      .usage = usages,
      .sharingMode = vk::SharingMode::eExclusive,
      .initialLayout = vk::ImageLayout::eUndefined,
  };
  auto image = VK_EXPECT_RV(mDevice->createImageUnique(imageCreateInfo));

  const auto memoryRequirements = mDevice->getImageMemoryRequirements(*image);

  const uint32_t memoryIndex = GetMemoryType(
      mPhysicalDevice, memoryRequirements.memoryTypeBits, memoryProperties);

  const vk::MemoryAllocateInfo imageMemoryAllocateInfo = {
      .allocationSize = memoryRequirements.size,
      .memoryTypeIndex = memoryIndex,
  };
  auto imageMemory =
      VK_EXPECT_RV(mDevice->allocateMemoryUnique(imageMemoryAllocateInfo));

  VK_EXPECT_RESULT(mDevice->bindImageMemory(*image, *imageMemory, 0));

  const vk::ImageViewCreateInfo imageViewCreateInfo = {
      .pNext = &samplerConversionInfo,
      .image = *image,
      .viewType = vk::ImageViewType::e2D,
      .format = vk::Format::eG8B8R83Plane420Unorm,
      .components =
          {
              .r = vk::ComponentSwizzle::eIdentity,
              .g = vk::ComponentSwizzle::eIdentity,
              .b = vk::ComponentSwizzle::eIdentity,
              .a = vk::ComponentSwizzle::eIdentity,
          },
      .subresourceRange =
          {
              .aspectMask = vk::ImageAspectFlagBits::eColor,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };
  auto imageView =
      VK_EXPECT_RV(mDevice->createImageViewUnique(imageViewCreateInfo));

  VK_EXPECT_RESULT(DoCommandsImmediate([&](vk::UniqueCommandBuffer& cmd) {
    const std::vector<vk::ImageMemoryBarrier> imageMemoryBarriers = {
        vk::ImageMemoryBarrier{
            .srcAccessMask = {},
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *image,
            .subresourceRange =
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },

        },
    };
    cmd->pipelineBarrier(
        /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
        /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
        /*dependencyFlags=*/{},
        /*memoryBarriers=*/{},
        /*bufferMemoryBarriers=*/{},
        /*imageMemoryBarriers=*/imageMemoryBarriers);
    return vk::Result::eSuccess;
  }));

  return YuvImageWithMemory{
      .imageSamplerConversion = std::move(imageSamplerConversion),
      .imageSampler = std::move(imageSampler),
      .imageMemory = std::move(imageMemory),
      .image = std::move(image),
      .imageView = std::move(imageView),
  };
}

vk::Result Vk::LoadYuvImage(const vk::UniqueImage& image, uint32_t width,
                            uint32_t height,
                            const std::vector<uint8_t>& imageDataY,
                            const std::vector<uint8_t>& imageDataU,
                            const std::vector<uint8_t>& imageDataV,
                            vk::ImageLayout currentLayout,
                            vk::ImageLayout returnedLayout) {
  auto* mapped = reinterpret_cast<uint8_t*>(VK_TRY_RV(
      mDevice->mapMemory(*mStagingBufferMemory, 0, kStagingBufferSize)));

  const VkDeviceSize yOffset = 0;
  const VkDeviceSize uOffset = imageDataY.size();
  const VkDeviceSize vOffset = imageDataY.size() + imageDataU.size();
  std::memcpy(mapped + yOffset, imageDataY.data(), imageDataY.size());
  std::memcpy(mapped + uOffset, imageDataU.data(), imageDataU.size());
  std::memcpy(mapped + vOffset, imageDataV.data(), imageDataV.size());
  mDevice->unmapMemory(*mStagingBufferMemory);

  return DoCommandsImmediate([&](vk::UniqueCommandBuffer& cmd) {
    if (currentLayout != vk::ImageLayout::eTransferDstOptimal) {
      const std::vector<vk::ImageMemoryBarrier> imageMemoryBarriers = {
          vk::ImageMemoryBarrier{
              .srcAccessMask = vk::AccessFlagBits::eMemoryRead |
                               vk::AccessFlagBits::eMemoryWrite,
              .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
              .oldLayout = currentLayout,
              .newLayout = vk::ImageLayout::eTransferDstOptimal,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vk::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },

          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }

    const std::vector<vk::BufferImageCopy> imageCopyRegions = {
        vk::BufferImageCopy{
            .bufferOffset = yOffset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = vk::ImageAspectFlagBits::ePlane0,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageOffset =
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
            .imageExtent =
                {
                    .width = width,
                    .height = height,
                    .depth = 1,
                },
        },
        vk::BufferImageCopy{
            .bufferOffset = uOffset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = vk::ImageAspectFlagBits::ePlane1,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageOffset =
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
            .imageExtent =
                {
                    .width = width / 2,
                    .height = height / 2,
                    .depth = 1,
                },
        },
        vk::BufferImageCopy{
            .bufferOffset = vOffset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = vk::ImageAspectFlagBits::ePlane2,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            .imageOffset =
                {
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
            .imageExtent =
                {
                    .width = width / 2,
                    .height = height / 2,
                    .depth = 1,
                },
        },
    };
    cmd->copyBufferToImage(*mStagingBuffer, *image,
                           vk::ImageLayout::eTransferDstOptimal,
                           imageCopyRegions);

    if (returnedLayout != vk::ImageLayout::eTransferDstOptimal) {
      const std::vector<vk::ImageMemoryBarrier> imageMemoryBarriers = {
          vk::ImageMemoryBarrier{
              .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
              .dstAccessMask = vk::AccessFlagBits::eMemoryRead |
                               vk::AccessFlagBits::eMemoryWrite,
              .oldLayout = vk::ImageLayout::eTransferDstOptimal,
              .newLayout = returnedLayout,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vk::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },
          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }
    return vk::Result::eSuccess;
  });
}

gfxstream::expected<Vk::FramebufferWithAttachments, vk::Result>
Vk::CreateFramebuffer(uint32_t width, uint32_t height, vk::Format color_format,
                      vk::Format depth_format) {
  std::optional<Vk::ImageWithMemory> colorAttachment;
  if (color_format != vk::Format::eUndefined) {
    colorAttachment =
        GFXSTREAM_EXPECT(CreateImage(width, height, color_format,
                                     vk::ImageUsageFlagBits::eColorAttachment |
                                         vk::ImageUsageFlagBits::eTransferSrc,
                                     vk::MemoryPropertyFlagBits::eDeviceLocal,
                                     vk::ImageLayout::eColorAttachmentOptimal));
  }

  std::optional<Vk::ImageWithMemory> depthAttachment;
  if (depth_format != vk::Format::eUndefined) {
    depthAttachment = GFXSTREAM_EXPECT(
        CreateImage(width, height, depth_format,
                    vk::ImageUsageFlagBits::eDepthStencilAttachment |
                        vk::ImageUsageFlagBits::eTransferSrc,
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    vk::ImageLayout::eDepthStencilAttachmentOptimal));
  }

  std::vector<vk::AttachmentDescription> attachments;

  std::optional<vk::AttachmentReference> colorAttachment_reference;
  if (color_format != vk::Format::eUndefined) {
    attachments.push_back(vk::AttachmentDescription{
        .format = color_format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eClear,
        .stencilStoreOp = vk::AttachmentStoreOp::eStore,
        .initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
    });

    colorAttachment_reference = vk::AttachmentReference{
        .attachment = static_cast<uint32_t>(attachments.size() - 1),
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
  }

  std::optional<vk::AttachmentReference> depthAttachment_reference;
  if (depth_format != vk::Format::eUndefined) {
    attachments.push_back(vk::AttachmentDescription{
        .format = depth_format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eClear,
        .stencilStoreOp = vk::AttachmentStoreOp::eStore,
        .initialLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
    });

    depthAttachment_reference = vk::AttachmentReference{
        .attachment = static_cast<uint32_t>(attachments.size() - 1),
        .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
    };
  }

  vk::SubpassDependency dependency = {
      .srcSubpass = 0,
      .dstSubpass = 0,
      .srcStageMask = {},
      .dstStageMask = vk::PipelineStageFlagBits::eFragmentShader,
      .srcAccessMask = {},
      .dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead,
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
  };
  if (color_format != vk::Format::eUndefined) {
    dependency.srcStageMask |=
        vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask |=
        vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
  }
  if (depth_format != vk::Format::eUndefined) {
    dependency.srcStageMask |=
        vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask |=
        vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask |= vk::AccessFlagBits::eColorAttachmentWrite;
  }

  vk::SubpassDescription subpass = {
      .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
      .inputAttachmentCount = 0,
      .pInputAttachments = nullptr,
      .colorAttachmentCount = 0,
      .pColorAttachments = nullptr,
      .pResolveAttachments = nullptr,
      .pDepthStencilAttachment = nullptr,
      .pPreserveAttachments = nullptr,
  };
  if (color_format != vk::Format::eUndefined) {
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &*colorAttachment_reference;
  }
  if (depth_format != vk::Format::eUndefined) {
    subpass.pDepthStencilAttachment = &*depthAttachment_reference;
  }

  const vk::RenderPassCreateInfo renderpassCreateInfo = {
      .attachmentCount = static_cast<uint32_t>(attachments.size()),
      .pAttachments = attachments.data(),
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &dependency,
  };
  auto renderpass =
      VK_EXPECT_RV(mDevice->createRenderPassUnique(renderpassCreateInfo));

  std::vector<vk::ImageView> framebufferAttachments;
  if (colorAttachment) {
    framebufferAttachments.push_back(*colorAttachment->imageView);
  }
  if (depthAttachment) {
    framebufferAttachments.push_back(*depthAttachment->imageView);
  }
  const vk::FramebufferCreateInfo framebufferCreateInfo = {
      .renderPass = *renderpass,
      .attachmentCount = static_cast<uint32_t>(framebufferAttachments.size()),
      .pAttachments = framebufferAttachments.data(),
      .width = width,
      .height = height,
      .layers = 1,
  };
  auto framebuffer =
      VK_EXPECT_RV(mDevice->createFramebufferUnique(framebufferCreateInfo));

  return Vk::FramebufferWithAttachments{
      .colorAttachment = std::move(colorAttachment),
      .depthAttachment = std::move(depthAttachment),
      .renderpass = std::move(renderpass),
      .framebuffer = std::move(framebuffer),
  };
}

vk::Result Vk::DoCommandsImmediate(
    const std::function<vk::Result(vk::UniqueCommandBuffer&)>& func,
    const std::vector<vk::UniqueSemaphore>& semaphores_wait,
    const std::vector<vk::UniqueSemaphore>& semaphores_signal) {
  const vk::CommandBufferAllocateInfo commandBufferAllocateInfo = {
      .commandPool = *mCommandPool,
      .level = vk::CommandBufferLevel::ePrimary,
      .commandBufferCount = 1,
  };
  auto commandBuffers = VK_TRY_RV(
      mDevice->allocateCommandBuffersUnique(commandBufferAllocateInfo));
  auto commandBuffer = std::move(commandBuffers[0]);

  const vk::CommandBufferBeginInfo commandBufferBeginInfo = {
      .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
  };
  VK_TRY(commandBuffer->begin(commandBufferBeginInfo));
  VK_TRY(func(commandBuffer));
  VK_TRY(commandBuffer->end());

  std::vector<vk::CommandBuffer> commandBufferHandles;
  commandBufferHandles.push_back(*commandBuffer);

  std::vector<vk::Semaphore> semaphoreHandlesWait;
  semaphoreHandlesWait.reserve(semaphores_wait.size());
  for (const auto& s : semaphores_wait) {
    semaphoreHandlesWait.emplace_back(*s);
  }

  std::vector<vk::Semaphore> semaphoreHandlesSignal;
  semaphoreHandlesSignal.reserve(semaphores_signal.size());
  for (const auto& s : semaphores_signal) {
    semaphoreHandlesSignal.emplace_back(*s);
  }

  vk::SubmitInfo submitInfo = {
      .commandBufferCount = static_cast<uint32_t>(commandBufferHandles.size()),
      .pCommandBuffers = commandBufferHandles.data(),
  };
  if (!semaphoreHandlesWait.empty()) {
    submitInfo.waitSemaphoreCount =
        static_cast<uint32_t>(semaphoreHandlesWait.size());
    submitInfo.pWaitSemaphores = semaphoreHandlesWait.data();
  }
  if (!semaphoreHandlesSignal.empty()) {
    submitInfo.signalSemaphoreCount =
        static_cast<uint32_t>(semaphoreHandlesSignal.size());
    submitInfo.pSignalSemaphores = semaphoreHandlesSignal.data();
  }
  VK_TRY(mQueue.submit(submitInfo));
  return mQueue.waitIdle();
}

}  // namespace gfxstream
