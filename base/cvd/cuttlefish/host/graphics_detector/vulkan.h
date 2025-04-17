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

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "cuttlefish/host/graphics_detector/expected.h"

#include "vulkan/vulkan_raii.hpp"
#include "vulkan/vulkan_to_string.hpp"

namespace gfxstream {

#define VK_EXPECT(x)                                  \
  ({                                                  \
    auto expected = (x);                              \
    if (!expected.ok()) {                             \
      return gfxstream::unexpected(expected.error()); \
    };                                                \
    std::move(expected.value());                      \
  })

#define VK_EXPECT_RESULT(x)                 \
  do {                                      \
    vk::Result result = (x);                \
    if (result != vk::Result::eSuccess) {   \
      return gfxstream::unexpected(result); \
    }                                       \
  } while (0);

#define VK_EXPECT_RV(x)                           \
  ({                                              \
    auto vk_rv = (x);                             \
    if (vk_rv.result != vk::Result::eSuccess) {   \
      return gfxstream::unexpected(vk_rv.result); \
    };                                            \
    std::move(vk_rv.value);                       \
  })

#define VK_EXPECT_RV_OR_STRING(x)                                      \
  ({                                                                   \
    auto vk_rv = (x);                                                  \
    if (vk_rv.result != vk::Result::eSuccess) {                        \
      return gfxstream::unexpected(std::string("Failed to " #x ": ") + \
                                   vk::to_string(vk_rv.result));       \
    };                                                                 \
    std::move(vk_rv.value);                                            \
  })

#define VK_TRY(x)                         \
  do {                                    \
    vk::Result result = (x);              \
    if (result != vk::Result::eSuccess) { \
      return result;                      \
    }                                     \
  } while (0);

#define VK_TRY_RV(x)                            \
  ({                                            \
    auto vk_rv = (x);                           \
    if (vk_rv.result != vk::Result::eSuccess) { \
      return vk_rv.result;                      \
    };                                          \
    std::move(vk_rv.value);                     \
  })

class Vk {
 public:
  static gfxstream::expected<Vk, vk::Result> Load(
      const std::vector<std::string>& instance_extensions = {},
      const std::vector<std::string>& instance_layers = {},
      const std::vector<std::string>& device_extensions = {});

  Vk(const Vk&) = delete;
  Vk& operator=(const Vk&) = delete;

  Vk(Vk&&) = default;
  Vk& operator=(Vk&&) = default;

  struct BufferWithMemory {
    vk::UniqueBuffer buffer;
    vk::UniqueDeviceMemory bufferMemory;
  };
  gfxstream::expected<BufferWithMemory, vk::Result> CreateBuffer(
      vk::DeviceSize buffer_size, vk::BufferUsageFlags buffer_usages,
      vk::MemoryPropertyFlags buffer_memory_properties);
  gfxstream::expected<BufferWithMemory, vk::Result> CreateBufferWithData(
      vk::DeviceSize buffer_size, vk::BufferUsageFlags buffer_usages,
      vk::MemoryPropertyFlags buffer_memory_properties,
      const uint8_t* buffer_data);

  vk::Result DoCommandsImmediate(
      const std::function<vk::Result(vk::UniqueCommandBuffer&)>& func,
      const std::vector<vk::UniqueSemaphore>& semaphores_wait = {},
      const std::vector<vk::UniqueSemaphore>& semaphores_signal = {});

  struct ImageWithMemory {
    vk::UniqueImage image;
    vk::UniqueDeviceMemory imageMemory;
    vk::UniqueImageView imageView;
  };
  gfxstream::expected<ImageWithMemory, vk::Result> CreateImage(
      uint32_t width, uint32_t height, vk::Format format,
      vk::ImageUsageFlags usages, vk::MemoryPropertyFlags memory_properties,
      vk::ImageLayout returned_layout);

  gfxstream::expected<std::vector<uint8_t>, vk::Result> DownloadImage(
      uint32_t width, uint32_t height, const vk::UniqueImage& image,
      vk::ImageLayout current_layout, vk::ImageLayout returned_layout);

  struct YuvImageWithMemory {
    vk::UniqueSamplerYcbcrConversion imageSamplerConversion;
    vk::UniqueSampler imageSampler;
    vk::UniqueDeviceMemory imageMemory;
    vk::UniqueImage image;
    vk::UniqueImageView imageView;
  };
  gfxstream::expected<YuvImageWithMemory, vk::Result> CreateYuvImage(
      uint32_t width, uint32_t height, vk::ImageUsageFlags usages,
      vk::MemoryPropertyFlags memory_properties,
      vk::ImageLayout returned_layout);

  vk::Result LoadYuvImage(const vk::UniqueImage& image, uint32_t width,
                          uint32_t height,
                          const std::vector<uint8_t>& image_data_y,
                          const std::vector<uint8_t>& image_data_u,
                          const std::vector<uint8_t>& image_data_v,
                          vk::ImageLayout current_layout,
                          vk::ImageLayout returned_layout);

  struct FramebufferWithAttachments {
    std::optional<ImageWithMemory> colorAttachment;
    std::optional<ImageWithMemory> depthAttachment;
    vk::UniqueRenderPass renderpass;
    vk::UniqueFramebuffer framebuffer;
  };
  gfxstream::expected<FramebufferWithAttachments, vk::Result> CreateFramebuffer(
      uint32_t width, uint32_t height,
      vk::Format colorAttachmentFormat = vk::Format::eUndefined,
      vk::Format depthAttachmentFormat = vk::Format::eUndefined);

  vk::Instance& instance() { return *mInstance; }

  vk::Device& device() { return *mDevice; }

 private:
  Vk(vk::detail::DynamicLoader loader, vk::UniqueInstance instance,
     std::optional<vk::UniqueDebugUtilsMessengerEXT> debug,
     vk::PhysicalDevice physical_device, vk::UniqueDevice device,
     vk::Queue queue, uint32_t queue_family_index,
     vk::UniqueCommandPool command_pool, vk::UniqueBuffer stagingBuffer,
     vk::UniqueDeviceMemory stagingBufferMemory)
      : mLoader(std::move(loader)),
        mInstance(std::move(instance)),
        mDebugMessenger(std::move(debug)),
        mPhysicalDevice(std::move(physical_device)),
        mDevice(std::move(device)),
        mQueue(std::move(queue)),
        mQueueFamilyIndex(queue_family_index),
        mCommandPool(std::move(command_pool)),
        mStagingBuffer(std::move(stagingBuffer)),
        mStagingBufferMemory(std::move(stagingBufferMemory)) {}

  // Note: order is important for destruction.
  vk::detail::DynamicLoader mLoader;
  vk::UniqueInstance mInstance;
  std::optional<vk::UniqueDebugUtilsMessengerEXT> mDebugMessenger;
  vk::PhysicalDevice mPhysicalDevice;
  vk::UniqueDevice mDevice;
  vk::Queue mQueue;
  uint32_t mQueueFamilyIndex;
  vk::UniqueCommandPool mCommandPool;
  static constexpr const VkDeviceSize kStagingBufferSize = 32 * 1024 * 1024;
  vk::UniqueBuffer mStagingBuffer;
  vk::UniqueDeviceMemory mStagingBufferMemory;
};

}  // namespace gfxstream
