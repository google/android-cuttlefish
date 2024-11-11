// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sample_base.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace cuttlefish {
namespace {

constexpr const bool kEnableValidationLayers = false;

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void*) {
  if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    ALOGV("%s", pCallbackData->pMessage);
  } else if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    ALOGI("%s", pCallbackData->pMessage);
  } else if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    ALOGW("%s", pCallbackData->pMessage);
  } else if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    ALOGE("%s", pCallbackData->pMessage);
  }
  return VK_FALSE;
}

Result<uint32_t> GetMemoryType(const vkhpp::PhysicalDevice& physical_device,
                               uint32_t memory_type_mask,
                               vkhpp::MemoryPropertyFlags memoryProperties) {
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
  return Err("Failed to find memory type matching " +
             vkhpp::to_string(memoryProperties));
}

}  // namespace

Result<Ok> SampleBase::StartUpBase(
    const std::vector<std::string>& requestedInstanceExtensions,
    const std::vector<std::string>& requestedInstanceLayers,
    const std::vector<std::string>& requestedDeviceExtensions) {
  VULKAN_HPP_DEFAULT_DISPATCHER.init(
      mLoader.getProcAddress<PFN_vkGetInstanceProcAddr>(
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

  const vkhpp::ApplicationInfo applicationInfo = {
      .pApplicationName = "cuttlefish Sample App",
      .applicationVersion = 1,
      .pEngineName = "cuttlefish Sample App",
      .engineVersion = 1,
      .apiVersion = VK_API_VERSION_1_2,
  };
  const vkhpp::InstanceCreateInfo instanceCreateInfo = {
      .pApplicationInfo = &applicationInfo,
      .enabledLayerCount =
          static_cast<uint32_t>(requestedInstanceLayersChars.size()),
      .ppEnabledLayerNames = requestedInstanceLayersChars.data(),
      .enabledExtensionCount =
          static_cast<uint32_t>(requestedInstanceExtensionsChars.size()),
      .ppEnabledExtensionNames = requestedInstanceExtensionsChars.data(),
  };
  mInstance = VK_EXPECT_RV(vkhpp::createInstanceUnique(instanceCreateInfo));

  VULKAN_HPP_DEFAULT_DISPATCHER.init(*mInstance);

  std::optional<vkhpp::UniqueDebugUtilsMessengerEXT> debugMessenger;
  if (kEnableValidationLayers) {
    const vkhpp::DebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
        .messageSeverity =
            vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
            vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vkhpp::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vkhpp::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vkhpp::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vkhpp::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = VulkanDebugCallback,
        .pUserData = nullptr,
    };
    debugMessenger = VK_EXPECT_RV(
        mInstance->createDebugUtilsMessengerEXTUnique(debugCreateInfo));
  }

  const auto physicalDevices =
      VK_EXPECT_RV(mInstance->enumeratePhysicalDevices());
  mPhysicalDevice = std::move(physicalDevices[0]);

  std::unordered_set<std::string> availableDeviceExtensions;
  {
    const auto exts =
        VK_EXPECT_RV(mPhysicalDevice.enumerateDeviceExtensionProperties());
    for (const auto& ext : exts) {
      availableDeviceExtensions.emplace(ext.extensionName);
    }
  }
  const auto features2 =
      mPhysicalDevice
          .getFeatures2<vkhpp::PhysicalDeviceFeatures2,  //
                        vkhpp::PhysicalDeviceSamplerYcbcrConversionFeatures>();

  bool ycbcr_conversion_needed = false;

  std::vector<const char*> requestedDeviceExtensionsChars;
  requestedDeviceExtensionsChars.reserve(requestedDeviceExtensions.size());
  for (const auto& e : requestedDeviceExtensions) {
    if (e == std::string(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME)) {
      // The interface of VK_KHR_sampler_ycbcr_conversion was promoted to core
      // in Vulkan 1.1 but the feature/functionality is still optional. Check
      // here:
      const auto& sampler_features =
          features2.get<vkhpp::PhysicalDeviceSamplerYcbcrConversionFeatures>();

      if (sampler_features.samplerYcbcrConversion == VK_FALSE) {
        return Err("Physical device doesn't support samplerYcbcrConversion");
      }
      ycbcr_conversion_needed = true;
    } else {
      if (availableDeviceExtensions.find(e) ==
          availableDeviceExtensions.end()) {
        return Err("Physical device doesn't support extension " +
                   std::string(e));
      }
      requestedDeviceExtensionsChars.push_back(e.c_str());
    }
  }

  mQueueFamilyIndex = -1;
  {
    const auto props = mPhysicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < props.size(); i++) {
      const auto& prop = props[i];
      if (prop.queueFlags & vkhpp::QueueFlagBits::eGraphics) {
        mQueueFamilyIndex = i;
        break;
      }
    }
  }

  const float queue_priority = 1.0f;
  const vkhpp::DeviceQueueCreateInfo device_queue_create_info = {
      .queueFamilyIndex = mQueueFamilyIndex,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
  };
  const vkhpp::PhysicalDeviceVulkan11Features device_enable_features = {
      .samplerYcbcrConversion = ycbcr_conversion_needed,
  };
  const vkhpp::DeviceCreateInfo deviceCreateInfo = {
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
  mDevice = VK_EXPECT_RV(mPhysicalDevice.createDeviceUnique(deviceCreateInfo));
  mQueue = mDevice->getQueue(mQueueFamilyIndex, 0);

  mStagingBuffer =
      VK_EXPECT(CreateBuffer(kStagingBufferSize,
                             vkhpp::BufferUsageFlagBits::eTransferDst |
                                 vkhpp::BufferUsageFlagBits::eTransferSrc,
                             vkhpp::MemoryPropertyFlagBits::eHostVisible |
                                 vkhpp::MemoryPropertyFlagBits::eHostCoherent));

  const vkhpp::FenceCreateInfo fenceCreateInfo = {
      .flags = vkhpp::FenceCreateFlagBits::eSignaled,
  };
  const vkhpp::SemaphoreCreateInfo semaphoreCreateInfo = {};
  const vkhpp::CommandPoolCreateInfo commandPoolCreateInfo = {
      .flags = vkhpp::CommandPoolCreateFlagBits::eResetCommandBuffer,
      .queueFamilyIndex = mQueueFamilyIndex,
  };
  for (uint32_t i = 0; i < kMaxFramesInFlight; i++) {
    auto fence = VK_EXPECT_RV(mDevice->createFenceUnique(fenceCreateInfo));
    auto readyForRender =
        VK_EXPECT_RV(mDevice->createSemaphoreUnique(semaphoreCreateInfo));
    auto readyForPresent =
        VK_EXPECT_RV(mDevice->createSemaphoreUnique(semaphoreCreateInfo));
    auto commandPool =
        VK_EXPECT_RV(mDevice->createCommandPoolUnique(commandPoolCreateInfo));
    const vkhpp::CommandBufferAllocateInfo commandBufferAllocateInfo = {
        .commandPool = *commandPool,
        .level = vkhpp::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    auto commandBuffers = VK_EXPECT_RV(
        mDevice->allocateCommandBuffersUnique(commandBufferAllocateInfo));
    auto commandBuffer = std::move(commandBuffers[0]);
    mFrameObjects.push_back(PerFrameObjects{
        .readyFence = std::move(fence),
        .readyForRender = std::move(readyForRender),
        .readyForPresent = std::move(readyForPresent),
        .commandPool = std::move(commandPool),
        .commandBuffer = std::move(commandBuffer),
    });
  }

  return Ok{};
}

Result<Ok> SampleBase::CleanUpBase() {
  mDevice->waitIdle();

  return Ok{};
}

Result<SampleBase::BufferWithMemory> SampleBase::CreateBuffer(
    vkhpp::DeviceSize bufferSize, vkhpp::BufferUsageFlags bufferUsages,
    vkhpp::MemoryPropertyFlags bufferMemoryProperties) {
  const vkhpp::BufferCreateInfo bufferCreateInfo = {
      .size = static_cast<VkDeviceSize>(bufferSize),
      .usage = bufferUsages,
      .sharingMode = vkhpp::SharingMode::eExclusive,
  };
  auto buffer = VK_EXPECT_RV(mDevice->createBufferUnique(bufferCreateInfo));

  vkhpp::MemoryRequirements bufferMemoryRequirements{};
  mDevice->getBufferMemoryRequirements(*buffer, &bufferMemoryRequirements);

  const auto bufferMemoryType = VK_EXPECT(
      GetMemoryType(mPhysicalDevice, bufferMemoryRequirements.memoryTypeBits,
                    bufferMemoryProperties));

  const vkhpp::MemoryAllocateInfo bufferMemoryAllocateInfo = {
      .allocationSize = bufferMemoryRequirements.size,
      .memoryTypeIndex = bufferMemoryType,
  };
  auto bufferMemory =
      VK_EXPECT_RV(mDevice->allocateMemoryUnique(bufferMemoryAllocateInfo));

  VK_EXPECT_RESULT(mDevice->bindBufferMemory(*buffer, *bufferMemory, 0));

  return SampleBase::BufferWithMemory{
      .buffer = std::move(buffer),
      .bufferMemory = std::move(bufferMemory),
  };
}

Result<SampleBase::BufferWithMemory> SampleBase::CreateBufferWithData(
    vkhpp::DeviceSize bufferSize, vkhpp::BufferUsageFlags bufferUsages,
    vkhpp::MemoryPropertyFlags bufferMemoryProperties,
    const uint8_t* bufferData) {
  auto buffer = VK_EXPECT(CreateBuffer(
      bufferSize, bufferUsages | vkhpp::BufferUsageFlagBits::eTransferDst,
      bufferMemoryProperties));

  void* mapped = VK_EXPECT_RV(
      mDevice->mapMemory(*mStagingBuffer.bufferMemory, 0, kStagingBufferSize));

  std::memcpy(mapped, bufferData, bufferSize);

  mDevice->unmapMemory(*mStagingBuffer.bufferMemory);

  DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
    const std::vector<vkhpp::BufferCopy> regions = {
        vkhpp::BufferCopy{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = bufferSize,
        },
    };
    cmd->copyBuffer(*mStagingBuffer.buffer, *buffer.buffer, regions);
    return Ok{};
  });

  return std::move(buffer);
}

Result<SampleBase::ImageWithMemory> SampleBase::CreateImage(
    uint32_t width, uint32_t height, vkhpp::Format format,
    vkhpp::ImageUsageFlags usages, vkhpp::MemoryPropertyFlags memoryProperties,
    vkhpp::ImageLayout returnedLayout) {
  const vkhpp::ImageCreateInfo imageCreateInfo = {
      .imageType = vkhpp::ImageType::e2D,
      .format = format,
      .extent =
          {
              .width = width,
              .height = height,
              .depth = 1,
          },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = vkhpp::SampleCountFlagBits::e1,
      .tiling = vkhpp::ImageTiling::eOptimal,
      .usage = usages,
      .sharingMode = vkhpp::SharingMode::eExclusive,
      .initialLayout = vkhpp::ImageLayout::eUndefined,
  };
  auto image = VK_EXPECT_RV(mDevice->createImageUnique(imageCreateInfo));

  const auto memoryRequirements = mDevice->getImageMemoryRequirements(*image);
  const uint32_t memoryIndex = VK_EXPECT(GetMemoryType(
      mPhysicalDevice, memoryRequirements.memoryTypeBits, memoryProperties));

  const vkhpp::MemoryAllocateInfo imageMemoryAllocateInfo = {
      .allocationSize = memoryRequirements.size,
      .memoryTypeIndex = memoryIndex,
  };
  auto imageMemory =
      VK_EXPECT_RV(mDevice->allocateMemoryUnique(imageMemoryAllocateInfo));

  mDevice->bindImageMemory(*image, *imageMemory, 0);

  const vkhpp::ImageViewCreateInfo imageViewCreateInfo = {
      .image = *image,
      .viewType = vkhpp::ImageViewType::e2D,
      .format = format,
      .components =
          {
              .r = vkhpp::ComponentSwizzle::eIdentity,
              .g = vkhpp::ComponentSwizzle::eIdentity,
              .b = vkhpp::ComponentSwizzle::eIdentity,
              .a = vkhpp::ComponentSwizzle::eIdentity,
          },
      .subresourceRange =
          {
              .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };
  auto imageView =
      VK_EXPECT_RV(mDevice->createImageViewUnique(imageViewCreateInfo));

  VK_EXPECT(DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
    const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
        vkhpp::ImageMemoryBarrier{
            .srcAccessMask = {},
            .dstAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
            .oldLayout = vkhpp::ImageLayout::eUndefined,
            .newLayout = returnedLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *image,
            .subresourceRange =
                {
                    .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        },
    };
    cmd->pipelineBarrier(
        /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
        /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
        /*dependencyFlags=*/{},
        /*memoryBarriers=*/{},
        /*bufferMemoryBarriers=*/{},
        /*imageMemoryBarriers=*/imageMemoryBarriers);

    return Ok{};
  }));

  return ImageWithMemory{
      .image = std::move(image),
      .imageMemory = std::move(imageMemory),
      .imageView = std::move(imageView),
  };
}

Result<Ok> SampleBase::LoadImage(const vkhpp::UniqueImage& image,
                                 uint32_t width, uint32_t height,
                                 const std::vector<uint8_t>& imageData,
                                 vkhpp::ImageLayout currentLayout,
                                 vkhpp::ImageLayout returnedLayout) {
  if (imageData.size() > kStagingBufferSize) {
    return Err("Failed to load image: staging buffer not large enough.");
  }

  auto* mapped = reinterpret_cast<uint8_t*>(VK_TRY_RV(
      mDevice->mapMemory(*mStagingBuffer.bufferMemory, 0, kStagingBufferSize)));

  std::memcpy(mapped, imageData.data(), imageData.size());

  mDevice->unmapMemory(*mStagingBuffer.bufferMemory);

  return DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
    if (currentLayout != vkhpp::ImageLayout::eTransferDstOptimal) {
      const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
          vkhpp::ImageMemoryBarrier{
              .srcAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                               vkhpp::AccessFlagBits::eMemoryWrite,
              .dstAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
              .oldLayout = currentLayout,
              .newLayout = vkhpp::ImageLayout::eTransferDstOptimal,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },

          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }

    const std::vector<vkhpp::BufferImageCopy> imageCopyRegions = {
        vkhpp::BufferImageCopy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
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
    cmd->copyBufferToImage(*mStagingBuffer.buffer, *image,
                           vkhpp::ImageLayout::eTransferDstOptimal,
                           imageCopyRegions);

    if (returnedLayout != vkhpp::ImageLayout::eTransferDstOptimal) {
      const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
          vkhpp::ImageMemoryBarrier{
              .srcAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
              .dstAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                               vkhpp::AccessFlagBits::eMemoryWrite,
              .oldLayout = vkhpp::ImageLayout::eTransferDstOptimal,
              .newLayout = returnedLayout,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },
          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }
    return Ok{};
  });
}

Result<std::vector<uint8_t>> SampleBase::DownloadImage(
    uint32_t width, uint32_t height, const vkhpp::UniqueImage& image,
    vkhpp::ImageLayout currentLayout, vkhpp::ImageLayout returnedLayout) {
  VK_EXPECT(DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
    if (currentLayout != vkhpp::ImageLayout::eTransferSrcOptimal) {
      const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
          vkhpp::ImageMemoryBarrier{
              .srcAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                               vkhpp::AccessFlagBits::eMemoryWrite,
              .dstAccessMask = vkhpp::AccessFlagBits::eTransferRead,
              .oldLayout = currentLayout,
              .newLayout = vkhpp::ImageLayout::eTransferSrcOptimal,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },
          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }

    const std::vector<vkhpp::BufferImageCopy> regions = {
        vkhpp::BufferImageCopy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
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
    cmd->copyImageToBuffer(*image, vkhpp::ImageLayout::eTransferSrcOptimal,
                           *mStagingBuffer.buffer, regions);

    if (returnedLayout != vkhpp::ImageLayout::eTransferSrcOptimal) {
      const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
          vkhpp::ImageMemoryBarrier{
              .srcAccessMask = vkhpp::AccessFlagBits::eTransferRead,
              .dstAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                               vkhpp::AccessFlagBits::eMemoryWrite,
              .oldLayout = vkhpp::ImageLayout::eTransferSrcOptimal,
              .newLayout = returnedLayout,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },
          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }

    return Ok{};
  }));

  auto* mapped = reinterpret_cast<uint8_t*>(VK_EXPECT_RV(
      mDevice->mapMemory(*mStagingBuffer.bufferMemory, 0, kStagingBufferSize)));

  std::vector<uint8_t> outPixels;
  outPixels.resize(width * height * 4);

  std::memcpy(outPixels.data(), mapped, outPixels.size());

  mDevice->unmapMemory(*mStagingBuffer.bufferMemory);

  return outPixels;
}

Result<SampleBase::YuvImageWithMemory> SampleBase::CreateYuvImage(
    uint32_t width, uint32_t height, vkhpp::ImageUsageFlags usages,
    vkhpp::MemoryPropertyFlags memoryProperties, vkhpp::ImageLayout layout) {
  const vkhpp::SamplerYcbcrConversionCreateInfo conversionCreateInfo = {
      .format = vkhpp::Format::eG8B8R83Plane420Unorm,
      .ycbcrModel = vkhpp::SamplerYcbcrModelConversion::eYcbcr601,
      .ycbcrRange = vkhpp::SamplerYcbcrRange::eItuNarrow,
      .components =
          {
              .r = vkhpp::ComponentSwizzle::eIdentity,
              .g = vkhpp::ComponentSwizzle::eIdentity,
              .b = vkhpp::ComponentSwizzle::eIdentity,
              .a = vkhpp::ComponentSwizzle::eIdentity,
          },
      .xChromaOffset = vkhpp::ChromaLocation::eMidpoint,
      .yChromaOffset = vkhpp::ChromaLocation::eMidpoint,
      .chromaFilter = vkhpp::Filter::eLinear,
      .forceExplicitReconstruction = VK_FALSE,
  };
  auto imageSamplerConversion = VK_EXPECT_RV(
      mDevice->createSamplerYcbcrConversionUnique(conversionCreateInfo));

  const vkhpp::SamplerYcbcrConversionInfo samplerConversionInfo = {
      .conversion = *imageSamplerConversion,
  };
  const vkhpp::SamplerCreateInfo samplerCreateInfo = {
      .pNext = &samplerConversionInfo,
      .magFilter = vkhpp::Filter::eLinear,
      .minFilter = vkhpp::Filter::eLinear,
      .mipmapMode = vkhpp::SamplerMipmapMode::eNearest,
      .addressModeU = vkhpp::SamplerAddressMode::eClampToEdge,
      .addressModeV = vkhpp::SamplerAddressMode::eClampToEdge,
      .addressModeW = vkhpp::SamplerAddressMode::eClampToEdge,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_FALSE,
      .maxAnisotropy = 1.0f,
      .compareEnable = VK_FALSE,
      .compareOp = vkhpp::CompareOp::eLessOrEqual,
      .minLod = 0.0f,
      .maxLod = 0.25f,
      .borderColor = vkhpp::BorderColor::eIntTransparentBlack,
      .unnormalizedCoordinates = VK_FALSE,
  };
  auto imageSampler =
      VK_EXPECT_RV(mDevice->createSamplerUnique(samplerCreateInfo));

  const vkhpp::ImageCreateInfo imageCreateInfo = {
      .imageType = vkhpp::ImageType::e2D,
      .format = vkhpp::Format::eG8B8R83Plane420Unorm,
      .extent =
          {
              .width = width,
              .height = height,
              .depth = 1,
          },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = vkhpp::SampleCountFlagBits::e1,
      .tiling = vkhpp::ImageTiling::eOptimal,
      .usage = usages,
      .sharingMode = vkhpp::SharingMode::eExclusive,
      .initialLayout = vkhpp::ImageLayout::eUndefined,
  };
  auto image = VK_EXPECT_RV(mDevice->createImageUnique(imageCreateInfo));

  const auto memoryRequirements = mDevice->getImageMemoryRequirements(*image);

  const uint32_t memoryIndex = VK_EXPECT(GetMemoryType(
      mPhysicalDevice, memoryRequirements.memoryTypeBits, memoryProperties));

  const vkhpp::MemoryAllocateInfo imageMemoryAllocateInfo = {
      .allocationSize = memoryRequirements.size,
      .memoryTypeIndex = memoryIndex,
  };
  auto imageMemory =
      VK_EXPECT_RV(mDevice->allocateMemoryUnique(imageMemoryAllocateInfo));

  mDevice->bindImageMemory(*image, *imageMemory, 0);

  const vkhpp::ImageViewCreateInfo imageViewCreateInfo = {
      .pNext = &samplerConversionInfo,
      .image = *image,
      .viewType = vkhpp::ImageViewType::e2D,
      .format = vkhpp::Format::eG8B8R83Plane420Unorm,
      .components =
          {
              .r = vkhpp::ComponentSwizzle::eIdentity,
              .g = vkhpp::ComponentSwizzle::eIdentity,
              .b = vkhpp::ComponentSwizzle::eIdentity,
              .a = vkhpp::ComponentSwizzle::eIdentity,
          },
      .subresourceRange =
          {
              .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
              .baseMipLevel = 0,
              .levelCount = 1,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
  };
  auto imageView =
      VK_EXPECT_RV(mDevice->createImageViewUnique(imageViewCreateInfo));

  VK_EXPECT(DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
    const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
        vkhpp::ImageMemoryBarrier{
            .srcAccessMask = {},
            .dstAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
            .oldLayout = vkhpp::ImageLayout::eUndefined,
            .newLayout = layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *image,
            .subresourceRange =
                {
                    .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },

        },
    };
    cmd->pipelineBarrier(
        /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
        /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
        /*dependencyFlags=*/{},
        /*memoryBarriers=*/{},
        /*bufferMemoryBarriers=*/{},
        /*imageMemoryBarriers=*/imageMemoryBarriers);
    return Ok{};
  }));

  return YuvImageWithMemory{
      .imageSamplerConversion = std::move(imageSamplerConversion),
      .imageSampler = std::move(imageSampler),
      .imageMemory = std::move(imageMemory),
      .image = std::move(image),
      .imageView = std::move(imageView),
  };
}

Result<Ok> SampleBase::LoadYuvImage(const vkhpp::UniqueImage& image,
                                    uint32_t width, uint32_t height,
                                    const std::vector<uint8_t>& imageDataY,
                                    const std::vector<uint8_t>& imageDataU,
                                    const std::vector<uint8_t>& imageDataV,
                                    vkhpp::ImageLayout currentLayout,
                                    vkhpp::ImageLayout returnedLayout) {
  auto* mapped = reinterpret_cast<uint8_t*>(VK_TRY_RV(
      mDevice->mapMemory(*mStagingBuffer.bufferMemory, 0, kStagingBufferSize)));

  const VkDeviceSize yOffset = 0;
  const VkDeviceSize uOffset = imageDataY.size();
  const VkDeviceSize vOffset = imageDataY.size() + imageDataU.size();
  std::memcpy(mapped + yOffset, imageDataY.data(), imageDataY.size());
  std::memcpy(mapped + uOffset, imageDataU.data(), imageDataU.size());
  std::memcpy(mapped + vOffset, imageDataV.data(), imageDataV.size());
  mDevice->unmapMemory(*mStagingBuffer.bufferMemory);

  return DoCommandsImmediate([&](vkhpp::UniqueCommandBuffer& cmd) {
    if (currentLayout != vkhpp::ImageLayout::eTransferDstOptimal) {
      const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
          vkhpp::ImageMemoryBarrier{
              .srcAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                               vkhpp::AccessFlagBits::eMemoryWrite,
              .dstAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
              .oldLayout = currentLayout,
              .newLayout = vkhpp::ImageLayout::eTransferDstOptimal,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },

          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }

    const std::vector<vkhpp::BufferImageCopy> imageCopyRegions = {
        vkhpp::BufferImageCopy{
            .bufferOffset = yOffset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = vkhpp::ImageAspectFlagBits::ePlane0,
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
        vkhpp::BufferImageCopy{
            .bufferOffset = uOffset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = vkhpp::ImageAspectFlagBits::ePlane1,
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
        vkhpp::BufferImageCopy{
            .bufferOffset = vOffset,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource =
                {
                    .aspectMask = vkhpp::ImageAspectFlagBits::ePlane2,
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
    cmd->copyBufferToImage(*mStagingBuffer.buffer, *image,
                           vkhpp::ImageLayout::eTransferDstOptimal,
                           imageCopyRegions);

    if (returnedLayout != vkhpp::ImageLayout::eTransferDstOptimal) {
      const std::vector<vkhpp::ImageMemoryBarrier> imageMemoryBarriers = {
          vkhpp::ImageMemoryBarrier{
              .srcAccessMask = vkhpp::AccessFlagBits::eTransferWrite,
              .dstAccessMask = vkhpp::AccessFlagBits::eMemoryRead |
                               vkhpp::AccessFlagBits::eMemoryWrite,
              .oldLayout = vkhpp::ImageLayout::eTransferDstOptimal,
              .newLayout = returnedLayout,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = *image,
              .subresourceRange =
                  {
                      .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1,
                  },
          },
      };
      cmd->pipelineBarrier(
          /*srcStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vkhpp::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/imageMemoryBarriers);
    }
    return Ok{};
  });
}

Result<SampleBase::FramebufferWithAttachments> SampleBase::CreateFramebuffer(
    uint32_t width, uint32_t height, vkhpp::Format color_format,
    vkhpp::Format depth_format) {
  std::optional<SampleBase::ImageWithMemory> colorAttachment;
  if (color_format != vkhpp::Format::eUndefined) {
    colorAttachment =
        VK_EXPECT(CreateImage(width, height, color_format,
                              vkhpp::ImageUsageFlagBits::eColorAttachment |
                                  vkhpp::ImageUsageFlagBits::eTransferSrc,
                              vkhpp::MemoryPropertyFlagBits::eDeviceLocal,
                              vkhpp::ImageLayout::eColorAttachmentOptimal));
  }

  std::optional<SampleBase::ImageWithMemory> depthAttachment;
  if (depth_format != vkhpp::Format::eUndefined) {
    depthAttachment = VK_EXPECT(
        CreateImage(width, height, depth_format,
                    vkhpp::ImageUsageFlagBits::eDepthStencilAttachment |
                        vkhpp::ImageUsageFlagBits::eTransferSrc,
                    vkhpp::MemoryPropertyFlagBits::eDeviceLocal,
                    vkhpp::ImageLayout::eDepthStencilAttachmentOptimal));
  }

  std::vector<vkhpp::AttachmentDescription> attachments;

  std::optional<vkhpp::AttachmentReference> colorAttachment_reference;
  if (color_format != vkhpp::Format::eUndefined) {
    attachments.push_back(vkhpp::AttachmentDescription{
        .format = color_format,
        .samples = vkhpp::SampleCountFlagBits::e1,
        .loadOp = vkhpp::AttachmentLoadOp::eClear,
        .storeOp = vkhpp::AttachmentStoreOp::eStore,
        .stencilLoadOp = vkhpp::AttachmentLoadOp::eClear,
        .stencilStoreOp = vkhpp::AttachmentStoreOp::eStore,
        .initialLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
        .finalLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
    });

    colorAttachment_reference = vkhpp::AttachmentReference{
        .attachment = static_cast<uint32_t>(attachments.size() - 1),
        .layout = vkhpp::ImageLayout::eColorAttachmentOptimal,
    };
  }

  std::optional<vkhpp::AttachmentReference> depthAttachment_reference;
  if (depth_format != vkhpp::Format::eUndefined) {
    attachments.push_back(vkhpp::AttachmentDescription{
        .format = depth_format,
        .samples = vkhpp::SampleCountFlagBits::e1,
        .loadOp = vkhpp::AttachmentLoadOp::eClear,
        .storeOp = vkhpp::AttachmentStoreOp::eStore,
        .stencilLoadOp = vkhpp::AttachmentLoadOp::eClear,
        .stencilStoreOp = vkhpp::AttachmentStoreOp::eStore,
        .initialLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
        .finalLayout = vkhpp::ImageLayout::eColorAttachmentOptimal,
    });

    depthAttachment_reference = vkhpp::AttachmentReference{
        .attachment = static_cast<uint32_t>(attachments.size() - 1),
        .layout = vkhpp::ImageLayout::eDepthStencilAttachmentOptimal,
    };
  }

  vkhpp::SubpassDependency dependency = {
      .srcSubpass = 0,
      .dstSubpass = 0,
      .srcStageMask = {},
      .dstStageMask = vkhpp::PipelineStageFlagBits::eFragmentShader,
      .srcAccessMask = {},
      .dstAccessMask = vkhpp::AccessFlagBits::eInputAttachmentRead,
      .dependencyFlags = vkhpp::DependencyFlagBits::eByRegion,
  };
  if (color_format != vkhpp::Format::eUndefined) {
    dependency.srcStageMask |=
        vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask |=
        vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask |= vkhpp::AccessFlagBits::eColorAttachmentWrite;
  }
  if (depth_format != vkhpp::Format::eUndefined) {
    dependency.srcStageMask |=
        vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstStageMask |=
        vkhpp::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask |= vkhpp::AccessFlagBits::eColorAttachmentWrite;
  }

  vkhpp::SubpassDescription subpass = {
      .pipelineBindPoint = vkhpp::PipelineBindPoint::eGraphics,
      .inputAttachmentCount = 0,
      .pInputAttachments = nullptr,
      .colorAttachmentCount = 0,
      .pColorAttachments = nullptr,
      .pResolveAttachments = nullptr,
      .pDepthStencilAttachment = nullptr,
      .pPreserveAttachments = nullptr,
  };
  if (color_format != vkhpp::Format::eUndefined) {
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &*colorAttachment_reference;
  }
  if (depth_format != vkhpp::Format::eUndefined) {
    subpass.pDepthStencilAttachment = &*depthAttachment_reference;
  }

  const vkhpp::RenderPassCreateInfo renderpassCreateInfo = {
      .attachmentCount = static_cast<uint32_t>(attachments.size()),
      .pAttachments = attachments.data(),
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &dependency,
  };
  auto renderpass =
      VK_EXPECT_RV(mDevice->createRenderPassUnique(renderpassCreateInfo));

  std::vector<vkhpp::ImageView> framebufferAttachments;
  if (colorAttachment) {
    framebufferAttachments.push_back(*colorAttachment->imageView);
  }
  if (depthAttachment) {
    framebufferAttachments.push_back(*depthAttachment->imageView);
  }
  const vkhpp::FramebufferCreateInfo framebufferCreateInfo = {
      .renderPass = *renderpass,
      .attachmentCount = static_cast<uint32_t>(framebufferAttachments.size()),
      .pAttachments = framebufferAttachments.data(),
      .width = width,
      .height = height,
      .layers = 1,
  };
  auto framebuffer =
      VK_EXPECT_RV(mDevice->createFramebufferUnique(framebufferCreateInfo));

  return SampleBase::FramebufferWithAttachments{
      .colorAttachment = std::move(colorAttachment),
      .depthAttachment = std::move(depthAttachment),
      .renderpass = std::move(renderpass),
      .framebuffer = std::move(framebuffer),
  };
}

Result<Ok> SampleBase::DoCommandsImmediate(
    const std::function<Result<Ok>(vkhpp::UniqueCommandBuffer&)>& func,
    const std::vector<vkhpp::UniqueSemaphore>& semaphores_wait,
    const std::vector<vkhpp::UniqueSemaphore>& semaphores_signal) {
  const vkhpp::CommandPoolCreateInfo commandPoolCreateInfo = {
      .queueFamilyIndex = mQueueFamilyIndex,
  };
  auto commandPool =
      VK_EXPECT_RV(mDevice->createCommandPoolUnique(commandPoolCreateInfo));
  const vkhpp::CommandBufferAllocateInfo commandBufferAllocateInfo = {
      .commandPool = *commandPool,
      .level = vkhpp::CommandBufferLevel::ePrimary,
      .commandBufferCount = 1,
  };
  auto commandBuffers = VK_TRY_RV(
      mDevice->allocateCommandBuffersUnique(commandBufferAllocateInfo));
  auto commandBuffer = std::move(commandBuffers[0]);

  const vkhpp::CommandBufferBeginInfo commandBufferBeginInfo = {
      .flags = vkhpp::CommandBufferUsageFlagBits::eOneTimeSubmit,
  };
  commandBuffer->begin(commandBufferBeginInfo);
  VK_EXPECT(func(commandBuffer));
  commandBuffer->end();

  std::vector<vkhpp::CommandBuffer> commandBufferHandles;
  commandBufferHandles.push_back(*commandBuffer);

  std::vector<vkhpp::Semaphore> semaphoreHandlesWait;
  semaphoreHandlesWait.reserve(semaphores_wait.size());
  for (const auto& s : semaphores_wait) {
    semaphoreHandlesWait.emplace_back(*s);
  }

  std::vector<vkhpp::Semaphore> semaphoreHandlesSignal;
  semaphoreHandlesSignal.reserve(semaphores_signal.size());
  for (const auto& s : semaphores_signal) {
    semaphoreHandlesSignal.emplace_back(*s);
  }

  vkhpp::SubmitInfo submitInfo = {
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
  mQueue.submit(submitInfo);
  mQueue.waitIdle();

  return Ok{};
}

Result<Ok> SampleBase::SetWindow(ANativeWindow* window) {
  mDevice->waitIdle();

  VK_EXPECT(DestroySwapchainDependents());
  VK_EXPECT(DestroySwapchain());
  VK_EXPECT(DestroySurface());

  mWindow = window;

  if (mWindow != nullptr) {
    VK_EXPECT(CreateSurface());
    VK_EXPECT(CreateSwapchain());
  }

  return Ok{};
}

Result<Ok> SampleBase::RecreateSwapchain() {
  VK_EXPECT(DestroySwapchain());
  VK_EXPECT(CreateSwapchain());
  return Ok{};
}

Result<Ok> SampleBase::CreateSurface() {
  if (mWindow == nullptr) {
    return Err("Failed to create VkSurface: no window!");
  }

  const vkhpp::AndroidSurfaceCreateInfoKHR surfaceCreateInfo = {
      .window = mWindow,
  };
  mSurface =
      VK_EXPECT_RV(mInstance->createAndroidSurfaceKHR(surfaceCreateInfo));

  return Ok{};
}

Result<Ok> SampleBase::DestroySurface() {
  mSurface.reset();
  return Ok{};
}

Result<Ok> SampleBase::CreateSwapchain() {
  if (!mSurface) {
    return Err("Failed to CreateSwapchain(): missing VkSurface?");
  }

  const auto capabilities =
      VK_EXPECT_RV(mPhysicalDevice.getSurfaceCapabilitiesKHR(*mSurface));
  const vkhpp::Extent2D swapchainExtent = capabilities.currentExtent;

  const auto formats =
      VK_EXPECT_RV(mPhysicalDevice.getSurfaceFormatsKHR(*mSurface));
  ALOGI("Supported surface formats:");
  for (const auto& format : formats) {
    const std::string formatStr = vkhpp::to_string(format.format);
    const std::string colorspaceStr = vkhpp::to_string(format.colorSpace);
    ALOGI(" - format:%s colorspace:%s", formatStr.c_str(),
          colorspaceStr.c_str());
  }
  // Always supported by Android:
  const vkhpp::SurfaceFormatKHR swapchainFormat = vkhpp::SurfaceFormatKHR{
      .format = vkhpp::Format::eR8G8B8A8Unorm,
      .colorSpace = vkhpp::ColorSpaceKHR::eSrgbNonlinear,
  };

  const auto modes =
      VK_EXPECT_RV(mPhysicalDevice.getSurfacePresentModesKHR(*mSurface));
  ALOGI("Supported surface present modes:");
  for (const auto& mode : modes) {
    const std::string modeStr = vkhpp::to_string(mode);
    ALOGI(" - %s", modeStr.c_str());
  }

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  const vkhpp::SwapchainCreateInfoKHR swapchainCreateInfo = {
      .surface = *mSurface,
      .minImageCount = imageCount,
      .imageFormat = swapchainFormat.format,
      .imageColorSpace = swapchainFormat.colorSpace,
      .imageExtent = swapchainExtent,
      .imageArrayLayers = 1,
      .imageUsage = vkhpp::ImageUsageFlagBits::eColorAttachment,
      .imageSharingMode = vkhpp::SharingMode::eExclusive,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = vkhpp::CompositeAlphaFlagBitsKHR::eInherit,
      .presentMode = vkhpp::PresentModeKHR::eFifo,
      .clipped = VK_TRUE,
  };
  auto swapchain =
      VK_EXPECT_RV(mDevice->createSwapchainKHRUnique(swapchainCreateInfo));

  auto swapchainImages =
      VK_EXPECT_RV(mDevice->getSwapchainImagesKHR(*swapchain));

  std::vector<vkhpp::UniqueImageView> swapchainImageViews;  // Owning
  std::vector<vkhpp::ImageView> swapchainImageViewHandles;  // Unowning
  for (const auto& image : swapchainImages) {
    const vkhpp::ImageViewCreateInfo imageViewCreateInfo = {
        .image = image,
        .viewType = vkhpp::ImageViewType::e2D,
        .format = swapchainFormat.format,
        .components =
            {
                .r = vkhpp::ComponentSwizzle::eIdentity,
                .g = vkhpp::ComponentSwizzle::eIdentity,
                .b = vkhpp::ComponentSwizzle::eIdentity,
                .a = vkhpp::ComponentSwizzle::eIdentity,
            },
        .subresourceRange =
            {
                .aspectMask = vkhpp::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    auto imageView =
        VK_EXPECT_RV(mDevice->createImageViewUnique(imageViewCreateInfo));
    swapchainImageViewHandles.push_back(*imageView);
    swapchainImageViews.push_back(std::move(imageView));
  }

  mSwapchainObjects = SwapchainObjects{
      .swapchainFormat = swapchainFormat,
      .swapchainExtent = swapchainExtent,
      .swapchain = std::move(swapchain),
      .swapchainImages = std::move(swapchainImages),
      .swapchainImageViews = std::move(swapchainImageViews),
  };

  const SwapchainInfo swapchainInfo = {
      .swapchainFormat = swapchainFormat.format,
      .swapchainExtent = swapchainExtent,
      .swapchainImageViews = swapchainImageViewHandles,
  };
  VK_EXPECT(CreateSwapchainDependents(swapchainInfo));

  return Ok{};
}

Result<Ok> SampleBase::DestroySwapchain() {
  VK_EXPECT(DestroySwapchainDependents());

  mSwapchainObjects.reset();

  return Ok{};
}

Result<Ok> SampleBase::Render() {
  if (!mSwapchainObjects) {
    return Ok{};
  }

  mCurrentFrame = (mCurrentFrame + 1) % mFrameObjects.size();
  PerFrameObjects& perFrame = mFrameObjects[mCurrentFrame];

  VK_EXPECT_RESULT(
      mDevice->waitForFences({*perFrame.readyFence}, VK_TRUE, UINT64_MAX));
  VK_EXPECT_RESULT(mDevice->resetFences({*perFrame.readyFence}));

  const vkhpp::SwapchainKHR swapchain = *mSwapchainObjects->swapchain;

  uint32_t swapchainImageIndex = -1;
  vkhpp::Result result = mDevice->acquireNextImageKHR(swapchain, UINT64_MAX,
                                                      *perFrame.readyForRender,
                                                      {}, &swapchainImageIndex);
  if (result == vkhpp::Result::eErrorOutOfDateKHR) {
    return RecreateSwapchain();
  } else if (result != vkhpp::Result::eSuccess &&
             result != vkhpp::Result::eSuboptimalKHR) {
    return Err("Failed to acquire next image: " + vkhpp::to_string(result));
  }

  VK_EXPECT_RESULT(perFrame.commandBuffer->reset());
  const vkhpp::CommandBufferBeginInfo commandBufferBeginInfo = {
      .flags = vkhpp::CommandBufferUsageFlagBits::eOneTimeSubmit,
  };
  VK_EXPECT_RESULT(perFrame.commandBuffer->begin(commandBufferBeginInfo));
  const FrameInfo frameInfo = {
      .swapchainImageIndex = swapchainImageIndex,
      .commandBuffer = *perFrame.commandBuffer,
  };
  VK_EXPECT(RecordFrame(frameInfo));
  VK_EXPECT_RESULT(perFrame.commandBuffer->end());

  const std::vector<vkhpp::CommandBuffer> commandBufferHandles = {
      *perFrame.commandBuffer,
  };
  const std::vector<vkhpp::Semaphore> renderWaitSemaphores = {
      *perFrame.readyForRender,
  };
  const std::vector<vkhpp::PipelineStageFlags> renderWaitStages = {
      vkhpp::PipelineStageFlagBits::eBottomOfPipe,
  };
  const std::vector<vkhpp::Semaphore> renderSignalSemaphores = {
      *perFrame.readyForPresent,
  };
  const vkhpp::SubmitInfo submitInfo = {
      .commandBufferCount = static_cast<uint32_t>(commandBufferHandles.size()),
      .pCommandBuffers = commandBufferHandles.data(),
      .waitSemaphoreCount = static_cast<uint32_t>(renderWaitSemaphores.size()),
      .pWaitSemaphores = renderWaitSemaphores.data(),
      .pWaitDstStageMask = renderWaitStages.data(),
      .signalSemaphoreCount =
          static_cast<uint32_t>(renderSignalSemaphores.size()),
      .pSignalSemaphores = renderSignalSemaphores.data(),
  };
  mQueue.submit(submitInfo, *perFrame.readyFence);

  const std::vector<vkhpp::Semaphore> presentReadySemaphores = {
      *perFrame.readyForPresent};
  const vkhpp::PresentInfoKHR presentInfo = {
      .waitSemaphoreCount =
          static_cast<uint32_t>(presentReadySemaphores.size()),
      .pWaitSemaphores = presentReadySemaphores.data(),
      .swapchainCount = 1,
      .pSwapchains = &swapchain,
      .pImageIndices = &swapchainImageIndex,
  };
  result = mQueue.presentKHR(presentInfo);
  if (result == vkhpp::Result::eErrorOutOfDateKHR ||
      result == vkhpp::Result::eSuboptimalKHR) {
    VK_EXPECT(RecreateSwapchain());
  } else if (result != vkhpp::Result::eSuccess) {
    return Err("Failed to present image: " + vkhpp::to_string(result));
  }

  return Ok{};
}

}  // namespace cuttlefish
