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

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_to_string.hpp>

namespace cuttlefish {

// For a function:
//
//   android::base::expected<vk::Type, vk::Result> Foo();
//
// simplifies
//
//   auto obj_expect = Foo();
//   if (!obj_expect.ok()) {
//     return expect.error();
//   }
//   auto obj = std::move(obj.value());
//
// to
//
//   auto obj = VK_EXPECT(Foo());
#define VK_EXPECT(x)                                    \
  ({                                                    \
    auto vk_expect_android_base_expected = (x);         \
    if (!vk_expect_android_base_expected.ok()) {        \
      return android::base::unexpected(                 \
          vk_expect_android_base_expected.error());     \
    };                                                  \
    std::move(vk_expect_android_base_expected.value()); \
  })

#define VK_EXPECT_RESULT(x)                             \
  ({                                                    \
    auto vk_expect_android_base_expected = (x);         \
    if (!vk_expect_android_base_expected.ok()) {        \
      return vk_expect_android_base_expected.error();   \
    };                                                  \
    std::move(vk_expect_android_base_expected.value()); \
  })

#define VK_RETURN_IF_NOT_SUCCESS(x)                    \
  do {                                                 \
    vk::Result result = (x);                           \
    if (result != vk::Result::eSuccess) return result; \
  } while (0);

#define VK_RETURN_UNEXPECTED_IF_NOT_SUCCESS(x)  \
  do {                                          \
    vk::Result result = (x);                    \
    if (result != vk::Result::eSuccess) {       \
      return android::base::unexpected(result); \
    }                                           \
  } while (0);

#define VK_ASSERT(x)                                                          \
  do {                                                                        \
    if (vk::Result result = (x); result != vk::Result::eSuccess) {            \
      LOG(FATAL) << __FILE__ << ":" << __LINE__ << ":" << __PRETTY_FUNCTION__ \
                 << ": " << #x << " returned " << to_string(result);          \
    }                                                                         \
  } while (0);

template <typename VkType>
using VkExpected = android::base::expected<VkType, vk::Result>;

class Vk {
 public:
  static std::optional<Vk> Load(
      const std::vector<std::string>& instance_extensions = {},
      const std::vector<std::string>& instance_layers = {},
      const std::vector<std::string>& device_extensions = {});

  Vk(const Vk&) = delete;
  Vk& operator=(const Vk&) = delete;

  Vk(Vk&&) = default;
  Vk& operator=(Vk&&) = default;

  // Note: order is important for destruction.
 private:
  static VkExpected<Vk> LoadImpl(
      const std::vector<std::string>& instance_extensions = {},
      const std::vector<std::string>& instance_layers = {},
      const std::vector<std::string>& device_extensions = {});

  vk::DynamicLoader vk_loader_;
  vk::raii::Context vk_context_;

 public:
  vk::raii::Instance vk_instance;

 private:
  std::optional<vk::raii::DebugUtilsMessengerEXT> vk_debug_messenger_;

 public:
  vk::raii::PhysicalDevice vk_physical_device;
  vk::raii::Device vk_device;
  vk::raii::Queue vk_queue;
  uint32_t vk_queue_family_index;

 private:
  vk::raii::CommandPool vk_command_pool_;
  static constexpr const VkDeviceSize kStagingBufferSize = 32 * 1024 * 1024;
  vk::raii::Buffer vk_staging_buffer_;
  vk::raii::DeviceMemory vk_staging_buffer_memory_;

 public:
  struct BufferWithMemory {
    vk::raii::Buffer buffer;
    vk::raii::DeviceMemory buffer_memory;
  };

  VkExpected<BufferWithMemory> CreateBuffer(
      vk::DeviceSize buffer_size, vk::BufferUsageFlags buffer_usages,
      vk::MemoryPropertyFlags buffer_memory_properties);

  VkExpected<BufferWithMemory> CreateBufferWithData(
      vk::DeviceSize buffer_size, vk::BufferUsageFlags buffer_usages,
      vk::MemoryPropertyFlags buffer_memory_properties,
      const uint8_t* buffer_data);

  vk::Result DoCommandsImmediate(
      std::function<vk::Result(vk::raii::CommandBuffer&)> func,
      std::vector<vk::raii::Semaphore> semaphores_wait = {},
      std::vector<vk::raii::Semaphore> semaphores_signal = {});

  struct ImageWithMemory {
    vk::raii::DeviceMemory image_memory;
    vk::raii::Image image;
    vk::raii::ImageView image_view;
  };
  VkExpected<ImageWithMemory> CreateImage(
      uint32_t width, uint32_t height, vk::Format format,
      vk::ImageUsageFlags usages, vk::MemoryPropertyFlags memory_properties,
      vk::ImageLayout returned_layout);

  vk::Result DownloadImage(uint32_t width, uint32_t height,
                           const vk::raii::Image& image,
                           vk::ImageLayout current_layout,
                           vk::ImageLayout returned_layout,
                           std::vector<uint8_t>* out_pixels);

  struct YuvImageWithMemory {
    vk::raii::SamplerYcbcrConversion image_sampler_conversion;
    vk::raii::Sampler image_sampler;
    vk::raii::DeviceMemory image_memory;
    vk::raii::Image image;
    vk::raii::ImageView image_view;
  };

  VkExpected<YuvImageWithMemory> CreateYuvImage(
      uint32_t width, uint32_t height, vk::ImageUsageFlags usages,
      vk::MemoryPropertyFlags memory_properties,
      vk::ImageLayout returned_layout);

  vk::Result LoadYuvImage(const vk::raii::Image& image, uint32_t width,
                          uint32_t height,
                          const std::vector<uint8_t>& image_data_y,
                          const std::vector<uint8_t>& image_data_u,
                          const std::vector<uint8_t>& image_data_v,
                          vk::ImageLayout current_layout,
                          vk::ImageLayout returned_layout);

  struct FramebufferWithAttachments {
    std::optional<ImageWithMemory> color_attachment;
    std::optional<ImageWithMemory> depth_attachment;
    vk::raii::RenderPass renderpass;
    vk::raii::Framebuffer framebuffer;
  };
  VkExpected<FramebufferWithAttachments> CreateFramebuffer(
      uint32_t width, uint32_t height,
      vk::Format color_format = vk::Format::eUndefined,
      vk::Format depth_format = vk::Format::eUndefined);

 private:
  Vk(vk::DynamicLoader loader, vk::raii::Context context,
     vk::raii::Instance instance,
     std::optional<vk::raii::DebugUtilsMessengerEXT> debug,
     vk::raii::PhysicalDevice physical_device, vk::raii::Device device,
     vk::raii::Queue queue, uint32_t queue_family_index,
     vk::raii::CommandPool command_pool, vk::raii::Buffer staging_buffer,
     vk::raii::DeviceMemory staging_buffer_memory)
      : vk_loader_(std::move(loader)),
        vk_context_(std::move(context)),
        vk_instance(std::move(instance)),
        vk_debug_messenger_(std::move(debug)),
        vk_physical_device(std::move(physical_device)),
        vk_device(std::move(device)),
        vk_queue(std::move(queue)),
        vk_queue_family_index(queue_family_index),
        vk_command_pool_(std::move(command_pool)),
        vk_staging_buffer_(std::move(staging_buffer)),
        vk_staging_buffer_memory_(std::move(staging_buffer_memory)) {}
};

}  // namespace cuttlefish
