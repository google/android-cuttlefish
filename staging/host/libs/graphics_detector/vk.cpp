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

#include "host/libs/graphics_detector/vk.h"

#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace cuttlefish {
namespace {

constexpr const bool kEnableValidationLayers = true;

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void*) {
  if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    LOG(VERBOSE) << pCallbackData->pMessage;
  } else if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    LOG(INFO) << pCallbackData->pMessage;
  } else if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    LOG(ERROR) << pCallbackData->pMessage;
  } else if (severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    LOG(ERROR) << pCallbackData->pMessage;
  }
  return VK_FALSE;
}

uint32_t GetMemoryType(const vk::raii::PhysicalDevice& physical_device,
                       uint32_t memory_type_mask,
                       vk::MemoryPropertyFlags memory_properties) {
  const auto props = physical_device.getMemoryProperties();
  for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
    if (!(memory_type_mask & (1 << i))) {
      continue;
    }
    if ((props.memoryTypes[i].propertyFlags & memory_properties) !=
        memory_properties) {
      continue;
    }
    return i;
  }
  return -1;
}

VkExpected<Vk::BufferWithMemory> DoCreateBuffer(
    const vk::raii::PhysicalDevice& physical_device,
    const vk::raii::Device& device, vk::DeviceSize buffer_size,
    vk::BufferUsageFlags buffer_usages,
    vk::MemoryPropertyFlags buffer_memory_properties) {
  const vk::BufferCreateInfo buffer_create_info = {
      .size = static_cast<VkDeviceSize>(buffer_size),
      .usage = buffer_usages,
      .sharingMode = vk::SharingMode::eExclusive,
  };
  auto buffer = VK_EXPECT(vk::raii::Buffer::create(device, buffer_create_info));

  const auto buffer_memory_requirements = buffer.getMemoryRequirements();
  const auto buffer_memory_type =
      GetMemoryType(physical_device, buffer_memory_requirements.memoryTypeBits,
                    buffer_memory_properties);

  const vk::MemoryAllocateInfo buffer_memory_allocate_info = {
      .allocationSize = buffer_memory_requirements.size,
      .memoryTypeIndex = buffer_memory_type,
  };
  auto buffer_memory = VK_EXPECT(
      vk::raii::DeviceMemory::create(device, buffer_memory_allocate_info));

  buffer.bindMemory(*buffer_memory, 0);

  return Vk::BufferWithMemory{
      .buffer = std::move(buffer),
      .buffer_memory = std::move(buffer_memory),
  };
}

}  // namespace

/*static*/
std::optional<Vk> Vk::Load(
    const std::vector<std::string>& requested_instance_extensions,
    const std::vector<std::string>& requested_instance_layers,
    const std::vector<std::string>& requested_device_extensions) {
  VkExpected<Vk> vk =
      LoadImpl(requested_instance_extensions, requested_instance_layers,
               requested_device_extensions);
  if (vk.ok()) {
    return std::move(vk.value());
  }
  return std::nullopt;
}

/*static*/
VkExpected<Vk> Vk::LoadImpl(
    const std::vector<std::string>& requested_instance_extensions,
    const std::vector<std::string>& requested_instance_layers,
    const std::vector<std::string>& requested_device_extensions) {
  vk::DynamicLoader loader;
  VULKAN_HPP_DEFAULT_DISPATCHER.init(
      loader.getProcAddress<PFN_vkGetInstanceProcAddr>(
          "vkGetInstanceProcAddr"));

  vk::raii::Context context;

  const auto available_instance_layers =
      context.enumerateInstanceLayerProperties();
  LOG(DEBUG) << "Available instance layers:";
  for (const vk::LayerProperties& layer : available_instance_layers) {
    LOG(DEBUG) << layer.layerName;
  }
  LOG(DEBUG) << "";

  std::vector<const char*> requested_instance_extensions_chars;
  for (const auto& e : requested_instance_extensions) {
    requested_instance_extensions_chars.push_back(e.c_str());
  }
  if (kEnableValidationLayers) {
    requested_instance_extensions_chars.push_back(
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  std::vector<const char*> requested_instance_layers_chars;
  for (const auto& l : requested_instance_layers) {
    requested_instance_layers_chars.push_back(l.c_str());
  }

  const vk::ApplicationInfo applicationInfo{
      .pApplicationName = "Cuttlefish Graphics Detector",
      .applicationVersion = 1,
      .pEngineName = "Cuttlefish Graphics Detector",
      .engineVersion = 1,
      .apiVersion = VK_API_VERSION_1_2,
  };
  const vk::InstanceCreateInfo instance_create_info{
      .pApplicationInfo = &applicationInfo,
      .enabledLayerCount =
          static_cast<uint32_t>(requested_instance_layers_chars.size()),
      .ppEnabledLayerNames = requested_instance_layers_chars.data(),
      .enabledExtensionCount =
          static_cast<uint32_t>(requested_instance_extensions_chars.size()),
      .ppEnabledExtensionNames = requested_instance_extensions_chars.data(),
  };

  auto instance =
      VK_EXPECT(vk::raii::Instance::create(context, instance_create_info));

  std::optional<vk::raii::DebugUtilsMessengerEXT> debug_messenger;
  if (kEnableValidationLayers) {
    const vk::DebugUtilsMessengerCreateInfoEXT debug_create_info = {
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                           vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                       vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = VulkanDebugCallback,
        .pUserData = nullptr,
    };
    debug_messenger = VK_EXPECT(
        vk::raii::DebugUtilsMessengerEXT::create(instance, debug_create_info));
  }

  auto physical_devices =
      VK_EXPECT(vk::raii::PhysicalDevices::create(instance));

  LOG(DEBUG) << "Available physical devices:";
  for (const auto& physical_device : physical_devices) {
    const auto physical_device_props = physical_device.getProperties();
    LOG(DEBUG) << physical_device_props.deviceName;
  }
  LOG(DEBUG) << "";

  vk::raii::PhysicalDevice physical_device = std::move(physical_devices[0]);
  {
    const auto props = physical_device.getProperties();
    LOG(DEBUG) << "Selected physical device: " << props.deviceName;
    LOG(DEBUG) << "";
  }
  {
    const auto exts = physical_device.enumerateDeviceExtensionProperties();
    LOG(DEBUG) << "Available physical device extensions:";
    for (const auto& ext : exts) {
      LOG(DEBUG) << ext.extensionName;
    }
    LOG(DEBUG) << "";
  }

  std::vector<const char*> requested_device_extensions_chars;
  for (const auto& e : requested_device_extensions) {
    requested_device_extensions_chars.push_back(e.c_str());
  }

  uint32_t queue_family_index = -1;
  {
    const auto props = physical_device.getQueueFamilyProperties();
    for (uint32_t i = 0; i < props.size(); i++) {
      const auto& prop = props[i];
      if (prop.queueFlags & vk::QueueFlagBits::eGraphics) {
        queue_family_index = i;
        break;
      }
    }
  }
  LOG(DEBUG) << "Graphics queue family index: " << queue_family_index;

  const float queue_priority = 1.0f;
  const vk::DeviceQueueCreateInfo device_queue_create_info = {
      .queueFamilyIndex = queue_family_index,
      .queueCount = 1,
      .pQueuePriorities = &queue_priority,
  };
  const vk::PhysicalDeviceVulkan11Features device_enable_features = {
      .samplerYcbcrConversion = VK_TRUE,
  };
  const vk::DeviceCreateInfo device_create_info = {
      .pNext = &device_enable_features,
      .pQueueCreateInfos = &device_queue_create_info,
      .queueCreateInfoCount = 1,
      .enabledLayerCount =
          static_cast<uint32_t>(requested_instance_layers_chars.size()),
      .ppEnabledLayerNames = requested_instance_layers_chars.data(),
      .enabledExtensionCount =
          static_cast<uint32_t>(requested_device_extensions_chars.size()),
      .ppEnabledExtensionNames = requested_device_extensions_chars.data(),
  };
  auto device =
      VK_EXPECT(vk::raii::Device::create(physical_device, device_create_info));
  auto queue = vk::raii::Queue(device, queue_family_index, 0);

  const vk::CommandPoolCreateInfo command_pool_create_info = {
      .queueFamilyIndex = queue_family_index,
  };
  auto command_pool = VK_EXPECT(
      vk::raii::CommandPool::create(device, command_pool_create_info));

  auto staging_buffer =
      VK_EXPECT(DoCreateBuffer(physical_device, device, kStagingBufferSize,
                               vk::BufferUsageFlagBits::eTransferDst |
                                   vk::BufferUsageFlagBits::eTransferSrc,
                               vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent));

  return Vk(std::move(loader), std::move(context), std::move(instance),
            std::move(debug_messenger), std::move(physical_device),
            std::move(device), std::move(queue), queue_family_index,
            std::move(command_pool), std::move(staging_buffer.buffer),
            std::move(staging_buffer.buffer_memory));
}

VkExpected<Vk::BufferWithMemory> Vk::CreateBuffer(
    vk::DeviceSize buffer_size, vk::BufferUsageFlags buffer_usages,
    vk::MemoryPropertyFlags buffer_memory_properties) {
  return DoCreateBuffer(vk_physical_device, vk_device, buffer_size,
                        buffer_usages, buffer_memory_properties);
}

VkExpected<Vk::BufferWithMemory> Vk::CreateBufferWithData(
    vk::DeviceSize buffer_size, vk::BufferUsageFlags buffer_usages,
    vk::MemoryPropertyFlags buffer_memory_properties,
    const uint8_t* buffer_data) {
  auto buffer = VK_EXPECT(CreateBuffer(
      buffer_size, buffer_usages | vk::BufferUsageFlagBits::eTransferDst,
      buffer_memory_properties));

  void* mapped = vk_staging_buffer_memory_.mapMemory(0, kStagingBufferSize);
  if (mapped == nullptr) {
    LOG(FATAL) << "Failed to map staging buffer.";
  }

  std::memcpy(mapped, buffer_data, buffer_size);
  vk_staging_buffer_memory_.unmapMemory();

  DoCommandsImmediate([&](vk::raii::CommandBuffer& cmd) {
    const std::vector<vk::BufferCopy> regions = {
        vk::BufferCopy{
            .srcOffset = 0,
            .dstOffset = 0,
            .size = buffer_size,
        },
    };
    cmd.copyBuffer(*vk_staging_buffer_, *buffer.buffer, regions);
    return vk::Result::eSuccess;
  });

  return std::move(buffer);
}

VkExpected<Vk::ImageWithMemory> Vk::CreateImage(
    uint32_t width, uint32_t height, vk::Format format,
    vk::ImageUsageFlags usages, vk::MemoryPropertyFlags memory_properties,
    vk::ImageLayout returned_layout) {
  const vk::ImageCreateInfo image_create_info = {
      .imageType = vk::ImageType::e2D,
      .extent.width = width,
      .extent.height = height,
      .extent.depth = 1,
      .mipLevels = 1,
      .arrayLayers = 1,
      .format = format,
      .tiling = vk::ImageTiling::eOptimal,
      .initialLayout = vk::ImageLayout::eUndefined,
      .usage = usages,
      .sharingMode = vk::SharingMode::eExclusive,
      .samples = vk::SampleCountFlagBits::e1,
  };
  auto image = VK_EXPECT(vk::raii::Image::create(vk_device, image_create_info));

  vk::MemoryRequirements memory_requirements = image.getMemoryRequirements();
  const uint32_t memory_index =
      GetMemoryType(vk_physical_device, memory_requirements.memoryTypeBits,
                    memory_properties);

  const vk::MemoryAllocateInfo image_memory_allocate_info = {
      .allocationSize = memory_requirements.size,
      .memoryTypeIndex = memory_index,
  };
  auto image_memory = VK_EXPECT(
      vk::raii::DeviceMemory::create(vk_device, image_memory_allocate_info));

  image.bindMemory(*image_memory, 0);

  const vk::ImageViewCreateInfo image_view_create_info = {
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
  auto image_view =
      VK_EXPECT(vk::raii::ImageView::create(vk_device, image_view_create_info));

  VK_ASSERT(DoCommandsImmediate([&](vk::raii::CommandBuffer& command_buffer) {
    const std::vector<vk::ImageMemoryBarrier> image_memory_barriers = {
        vk::ImageMemoryBarrier{
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = returned_layout,
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
            .srcAccessMask = {},
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        },
    };
    command_buffer.pipelineBarrier(
        /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
        /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
        /*dependencyFlags=*/{},
        /*memoryBarriers=*/{},
        /*bufferMemoryBarriers=*/{},
        /*imageMemoryBarriers=*/image_memory_barriers);

    return vk::Result::eSuccess;
  }));

  return ImageWithMemory{
      .image_memory = std::move(image_memory),
      .image = std::move(image),
      .image_view = std::move(image_view),
  };
}

vk::Result Vk::DownloadImage(uint32_t width, uint32_t height,
                             const vk::raii::Image& image,
                             vk::ImageLayout current_layout,
                             vk::ImageLayout returned_layout,
                             std::vector<uint8_t>* out_pixels) {
  VK_RETURN_IF_NOT_SUCCESS(
      DoCommandsImmediate([&](vk::raii::CommandBuffer& command_buffer) {
        if (current_layout != vk::ImageLayout::eTransferSrcOptimal) {
          const std::vector<vk::ImageMemoryBarrier> image_memory_barriers = {
              vk::ImageMemoryBarrier{
                  .oldLayout = current_layout,
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
                  .srcAccessMask = vk::AccessFlagBits::eMemoryRead |
                                   vk::AccessFlagBits::eMemoryWrite,
                  .dstAccessMask = vk::AccessFlagBits::eTransferRead,
              },
          };
          command_buffer.pipelineBarrier(
              /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
              /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
              /*dependencyFlags=*/{},
              /*memoryBarriers=*/{},
              /*bufferMemoryBarriers=*/{},
              /*imageMemoryBarriers=*/image_memory_barriers);
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
        command_buffer.copyImageToBuffer(*image,
                                         vk::ImageLayout::eTransferSrcOptimal,
                                         *vk_staging_buffer_, regions);

        if (returned_layout != vk::ImageLayout::eTransferSrcOptimal) {
          const std::vector<vk::ImageMemoryBarrier> image_memory_barriers = {
              vk::ImageMemoryBarrier{
                  .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                  .newLayout = returned_layout,
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
                  .srcAccessMask = vk::AccessFlagBits::eTransferRead,
                  .dstAccessMask = vk::AccessFlagBits::eMemoryRead |
                                   vk::AccessFlagBits::eMemoryWrite,
              },
          };
          command_buffer.pipelineBarrier(
              /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
              /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
              /*dependencyFlags=*/{},
              /*memoryBarriers=*/{},
              /*bufferMemoryBarriers=*/{},
              /*imageMemoryBarriers=*/image_memory_barriers);
        }

        return vk::Result::eSuccess;
      }));

  auto* mapped = reinterpret_cast<uint8_t*>(
      vk_staging_buffer_memory_.mapMemory(0, kStagingBufferSize));
  if (mapped == nullptr) {
    LOG(ERROR) << "Failed to map staging buffer.";
    return vk::Result::eErrorMemoryMapFailed;
  }

  out_pixels->clear();
  out_pixels->resize(width * height * 4);
  std::memcpy(out_pixels->data(), mapped, out_pixels->size());
  vk_staging_buffer_memory_.unmapMemory();

  return vk::Result::eSuccess;
}

VkExpected<Vk::YuvImageWithMemory> Vk::CreateYuvImage(
    uint32_t width, uint32_t height, vk::ImageUsageFlags usages,
    vk::MemoryPropertyFlags memory_properties, vk::ImageLayout layout) {
  const vk::SamplerYcbcrConversionCreateInfo conversion_create_info = {
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
  auto image_sampler_conversion =
      VK_EXPECT(vk::raii::SamplerYcbcrConversion::create(
          vk_device, conversion_create_info));

  const vk::SamplerYcbcrConversionInfo sampler_conversion_info = {
      .conversion = *image_sampler_conversion,
  };
  const vk::SamplerCreateInfo sampler_create_info = {
      .pNext = &sampler_conversion_info,
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
  auto image_sampler =
      VK_EXPECT(vk::raii::Sampler::create(vk_device, sampler_create_info));

  const vk::ImageCreateInfo image_create_info = {
      .imageType = vk::ImageType::e2D,
      .extent.width = width,
      .extent.height = height,
      .extent.depth = 1,
      .mipLevels = 1,
      .arrayLayers = 1,
      .format = vk::Format::eG8B8R83Plane420Unorm,
      .tiling = vk::ImageTiling::eOptimal,
      .initialLayout = vk::ImageLayout::eUndefined,
      .usage = usages,
      .sharingMode = vk::SharingMode::eExclusive,
      .samples = vk::SampleCountFlagBits::e1,
  };
  auto image = VK_EXPECT(vk::raii::Image::create(vk_device, image_create_info));

  vk::MemoryRequirements memory_requirements = image.getMemoryRequirements();

  const uint32_t memory_index =
      GetMemoryType(vk_physical_device, memory_requirements.memoryTypeBits,
                    memory_properties);

  const vk::MemoryAllocateInfo image_memory_allocate_info = {
      .allocationSize = memory_requirements.size,
      .memoryTypeIndex = memory_index,
  };
  auto image_memory = VK_EXPECT(
      vk::raii::DeviceMemory::create(vk_device, image_memory_allocate_info));

  image.bindMemory(*image_memory, 0);

  const vk::ImageViewCreateInfo image_view_create_info = {
      .pNext = &sampler_conversion_info,
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
  auto image_view =
      VK_EXPECT(vk::raii::ImageView::create(vk_device, image_view_create_info));

  VK_ASSERT(DoCommandsImmediate([&](vk::raii::CommandBuffer& command_buffer) {
    const std::vector<vk::ImageMemoryBarrier> image_memory_barriers = {
        vk::ImageMemoryBarrier{
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
            .srcAccessMask = {},
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        },
    };
    command_buffer.pipelineBarrier(
        /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
        /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
        /*dependencyFlags=*/{},
        /*memoryBarriers=*/{},
        /*bufferMemoryBarriers=*/{},
        /*imageMemoryBarriers=*/image_memory_barriers);
    return vk::Result::eSuccess;
  }));

  return YuvImageWithMemory{
      .image_sampler_conversion = std::move(image_sampler_conversion),
      .image_sampler = std::move(image_sampler),
      .image_memory = std::move(image_memory),
      .image = std::move(image),
      .image_view = std::move(image_view),
  };
}

vk::Result Vk::LoadYuvImage(const vk::raii::Image& image, uint32_t width,
                            uint32_t height,
                            const std::vector<uint8_t>& image_data_y,
                            const std::vector<uint8_t>& image_data_u,
                            const std::vector<uint8_t>& image_data_v,
                            vk::ImageLayout current_layout,
                            vk::ImageLayout returned_layout) {
  auto* mapped = reinterpret_cast<uint8_t*>(
      vk_staging_buffer_memory_.mapMemory(0, kStagingBufferSize));
  if (mapped == nullptr) {
    LOG(ERROR) << "Failed to map staging buffer.";
    return vk::Result::eErrorMemoryMapFailed;
  }

  const VkDeviceSize y_offset = 0;
  const VkDeviceSize u_offset = image_data_y.size();
  const VkDeviceSize v_offset = image_data_y.size() + image_data_u.size();
  std::memcpy(mapped + y_offset, image_data_y.data(), image_data_y.size());
  std::memcpy(mapped + u_offset, image_data_u.data(), image_data_u.size());
  std::memcpy(mapped + v_offset, image_data_v.data(), image_data_v.size());
  vk_staging_buffer_memory_.unmapMemory();

  return DoCommandsImmediate([&](vk::raii::CommandBuffer& command_buffer) {
    if (current_layout != vk::ImageLayout::eTransferDstOptimal) {
      const std::vector<vk::ImageMemoryBarrier> image_memory_barriers = {
          vk::ImageMemoryBarrier{
              .oldLayout = current_layout,
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
              .srcAccessMask = vk::AccessFlagBits::eMemoryRead |
                               vk::AccessFlagBits::eMemoryWrite,
              .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
          },
      };
      command_buffer.pipelineBarrier(
          /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/image_memory_barriers);
    }

    const std::vector<vk::BufferImageCopy> image_copy_regions = {
        vk::BufferImageCopy{
            .bufferOffset = y_offset,
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
            .bufferOffset = u_offset,
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
            .bufferOffset = v_offset,
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
    command_buffer.copyBufferToImage(*vk_staging_buffer_, *image,
                                     vk::ImageLayout::eTransferDstOptimal,
                                     image_copy_regions);

    if (returned_layout != vk::ImageLayout::eTransferDstOptimal) {
      const std::vector<vk::ImageMemoryBarrier> image_memory_barriers = {
          vk::ImageMemoryBarrier{
              .oldLayout = vk::ImageLayout::eTransferDstOptimal,
              .newLayout = returned_layout,
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
              .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
              .dstAccessMask = vk::AccessFlagBits::eMemoryRead |
                               vk::AccessFlagBits::eMemoryWrite,
          },
      };
      command_buffer.pipelineBarrier(
          /*srcStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dstStageMask=*/vk::PipelineStageFlagBits::eAllCommands,
          /*dependencyFlags=*/{},
          /*memoryBarriers=*/{},
          /*bufferMemoryBarriers=*/{},
          /*imageMemoryBarriers=*/image_memory_barriers);
    }
    return vk::Result::eSuccess;
  });
}

VkExpected<Vk::FramebufferWithAttachments> Vk::CreateFramebuffer(
    uint32_t width, uint32_t height, vk::Format color_format,
    vk::Format depth_format) {
  std::optional<Vk::ImageWithMemory> color_attachment;
  if (color_format != vk::Format::eUndefined) {
    color_attachment =
        VK_EXPECT(CreateImage(width, height, color_format,
                              vk::ImageUsageFlagBits::eColorAttachment |
                                  vk::ImageUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eDeviceLocal,
                              vk::ImageLayout::eColorAttachmentOptimal));
  }

  std::optional<Vk::ImageWithMemory> depth_attachment;
  if (depth_format != vk::Format::eUndefined) {
    depth_attachment =
        VK_EXPECT(CreateImage(width, height, depth_format,
                              vk::ImageUsageFlagBits::eDepthStencilAttachment |
                                  vk::ImageUsageFlagBits::eTransferSrc,
                              vk::MemoryPropertyFlagBits::eDeviceLocal,
                              vk::ImageLayout::eDepthStencilAttachmentOptimal));
  }

  std::vector<vk::AttachmentDescription> attachments;

  std::optional<vk::AttachmentReference> color_attachment_reference;
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

    color_attachment_reference = vk::AttachmentReference{
        .attachment = static_cast<uint32_t>(attachments.size() - 1),
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
  }

  std::optional<vk::AttachmentReference> depth_attachment_reference;
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

    depth_attachment_reference = vk::AttachmentReference{
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
    subpass.pColorAttachments = &*color_attachment_reference;
  }
  if (depth_format != vk::Format::eUndefined) {
    subpass.pDepthStencilAttachment = &*depth_attachment_reference;
  }

  const vk::RenderPassCreateInfo renderpass_create_info = {
      .attachmentCount = static_cast<uint32_t>(attachments.size()),
      .pAttachments = attachments.data(),
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &dependency,
  };
  auto renderpass = VK_EXPECT(
      vk::raii::RenderPass::create(vk_device, renderpass_create_info));

  std::vector<vk::ImageView> frammebuffer_attachments;
  if (color_attachment) {
    frammebuffer_attachments.push_back(*color_attachment->image_view);
  }
  if (depth_attachment) {
    frammebuffer_attachments.push_back(*depth_attachment->image_view);
  }
  const vk::FramebufferCreateInfo framebuffer_create_info = {
      .renderPass = *renderpass,
      .attachmentCount = static_cast<uint32_t>(frammebuffer_attachments.size()),
      .pAttachments = frammebuffer_attachments.data(),
      .width = width,
      .height = height,
      .layers = 1,
  };
  auto framebuffer = VK_EXPECT(
      vk::raii::Framebuffer::create(vk_device, framebuffer_create_info));

  return Vk::FramebufferWithAttachments{
      .color_attachment = std::move(color_attachment),
      .depth_attachment = std::move(depth_attachment),
      .renderpass = std::move(renderpass),
      .framebuffer = std::move(framebuffer),
  };
}

vk::Result Vk::DoCommandsImmediate(
    std::function<vk::Result(vk::raii::CommandBuffer&)> func,
    std::vector<vk::raii::Semaphore> semaphores_wait,
    std::vector<vk::raii::Semaphore> semaphores_signal) {
  const vk::CommandBufferAllocateInfo command_buffer_allocate_info = {
      .level = vk::CommandBufferLevel::ePrimary,
      .commandPool = *vk_command_pool_,
      .commandBufferCount = 1,
  };
  auto command_buffers = VK_EXPECT_RESULT(vk::raii::CommandBuffers::create(
      vk_device, command_buffer_allocate_info));
  auto command_buffer = std::move(command_buffers[0]);

  const vk::CommandBufferBeginInfo command_buffer_begin_info = {
      .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
  };
  command_buffer.begin(command_buffer_begin_info);

  VK_RETURN_IF_NOT_SUCCESS(func(command_buffer));

  command_buffer.end();

  std::vector<vk::CommandBuffer> command_buffer_handles;
  command_buffer_handles.push_back(*command_buffer);

  std::vector<vk::Semaphore> semaphores_handles_wait;
  for (const auto& s : semaphores_wait) {
    semaphores_handles_wait.emplace_back(*s);
  }

  std::vector<vk::Semaphore> semaphores_handles_signal;
  for (const auto& s : semaphores_signal) {
    semaphores_handles_signal.emplace_back(*s);
  }

  vk::SubmitInfo submit_info = {
      .commandBufferCount =
          static_cast<uint32_t>(command_buffer_handles.size()),
      .pCommandBuffers = command_buffer_handles.data(),
  };
  if (!semaphores_handles_wait.empty()) {
    submit_info.waitSemaphoreCount =
        static_cast<uint32_t>(semaphores_handles_wait.size());
    submit_info.pWaitSemaphores = semaphores_handles_wait.data();
  }
  if (!semaphores_handles_signal.empty()) {
    submit_info.signalSemaphoreCount =
        static_cast<uint32_t>(semaphores_handles_signal.size());
    submit_info.pSignalSemaphores = semaphores_handles_signal.data();
  }
  vk_queue.submit(submit_info);
  vk_queue.waitIdle();

  return vk::Result::eSuccess;
}

}  // namespace cuttlefish
