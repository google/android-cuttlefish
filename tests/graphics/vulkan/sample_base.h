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

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <android-base/expected.h>
#include <android/native_window_jni.h>
#define VULKAN_HPP_NAMESPACE vkhpp
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_to_string.hpp>

#include "common.h"

namespace cuttlefish {

template <typename T>
using Result = android::base::expected<T, std::string>;

// Empty object for `Result<Ok>` that allows using the below macros.
struct Ok {};

inline android::base::unexpected<std::string> Err(const std::string& msg) {
  return android::base::unexpected(msg);
}

#define VK_ASSERT(x)                                          \
  ({                                                          \
    auto result = (x);                                        \
    if (!result.ok()) {                                       \
      ALOGE("Failed to " #x ": %s.", result.error().c_str()); \
      std::abort();                                           \
    };                                                        \
    std::move(result.value());                                \
  })

#define VK_EXPECT(x)                \
  ({                                \
    auto expected = (x);            \
    if (!expected.ok()) {           \
      return Err(expected.error()); \
    };                              \
    std::move(expected.value());    \
  })

#define VK_EXPECT_RESULT(x)                          \
  do {                                               \
    vkhpp::Result result = (x);                      \
    if (result != vkhpp::Result::eSuccess) {         \
      return Err(std::string("Failed to " #x ": ") + \
                 vkhpp::to_string(result));          \
    }                                                \
  } while (0);

#define VK_EXPECT_RV(x)                               \
  ({                                                  \
    auto vkhpp_rv = (x);                              \
    if (vkhpp_rv.result != vkhpp::Result::eSuccess) { \
      return Err(std::string("Failed to " #x ": ") +  \
                 vkhpp::to_string(vkhpp_rv.result));  \
    };                                                \
    std::move(vkhpp_rv.value);                        \
  })

#define VK_TRY(x)                                    \
  do {                                               \
    vkhpp::Result result = (x);                      \
    if (result != vkhpp::Result::eSuccess) {         \
      return Err(std::string("Failed to " #x ": ") + \
                 vkhpp::to_string(result));          \
    }                                                \
  } while (0);

#define VK_TRY_RV(x)                                  \
  ({                                                  \
    auto vkhpp_rv = (x);                              \
    if (vkhpp_rv.result != vkhpp::Result::eSuccess) { \
      return Err(std::string("Failed to " #x ": ") +  \
                 vkhpp::to_string(vkhpp_rv.result));  \
    };                                                \
    std::move(vkhpp_rv.value);                        \
  })

class SampleBase {
 public:
  virtual ~SampleBase() {}

  SampleBase(const SampleBase&) = delete;
  SampleBase& operator=(const SampleBase&) = delete;

  SampleBase(SampleBase&&) = default;
  SampleBase& operator=(SampleBase&&) = default;

  virtual Result<Ok> StartUp() = 0;
  virtual Result<Ok> CleanUp() = 0;

  struct SwapchainInfo {
    vkhpp::Format swapchainFormat;
    vkhpp::Extent2D swapchainExtent;
    std::vector<vkhpp::ImageView> swapchainImageViews;
  };
  virtual Result<Ok> CreateSwapchainDependents(const SwapchainInfo& /*info*/) {
    return Ok{};
  }

  virtual Result<Ok> DestroySwapchainDependents() { return Ok{}; }

  struct FrameInfo {
    uint32_t swapchainImageIndex = -1;
    vkhpp::CommandBuffer commandBuffer;
  };
  virtual Result<Ok> RecordFrame(const FrameInfo& /*frame*/) { return Ok{}; }

  Result<Ok> Render();

  Result<Ok> SetWindow(ANativeWindow* window = nullptr);

 protected:
  SampleBase() = default;

  Result<Ok> StartUpBase(const std::vector<std::string>& instance_extensions =
                             {
                                 VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
                                 VK_KHR_SURFACE_EXTENSION_NAME,
                             },
                         const std::vector<std::string>& instance_layers = {},
                         const std::vector<std::string>& device_extensions = {
                             VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                         });
  Result<Ok> CleanUpBase();

  Result<Ok> CreateSurface();
  Result<Ok> DestroySurface();

  Result<Ok> CreateSwapchain();
  Result<Ok> DestroySwapchain();
  Result<Ok> RecreateSwapchain();

  struct BufferWithMemory {
    vkhpp::UniqueBuffer buffer;
    vkhpp::UniqueDeviceMemory bufferMemory;
  };
  Result<BufferWithMemory> CreateBuffer(
      vkhpp::DeviceSize buffer_size, vkhpp::BufferUsageFlags buffer_usages,
      vkhpp::MemoryPropertyFlags buffer_memory_properties);
  Result<BufferWithMemory> CreateBufferWithData(
      vkhpp::DeviceSize buffer_size, vkhpp::BufferUsageFlags buffer_usages,
      vkhpp::MemoryPropertyFlags buffer_memory_properties,
      const uint8_t* buffer_data);

  Result<Ok> DoCommandsImmediate(
      const std::function<Result<Ok>(vkhpp::UniqueCommandBuffer&)>& func,
      const std::vector<vkhpp::UniqueSemaphore>& semaphores_wait = {},
      const std::vector<vkhpp::UniqueSemaphore>& semaphores_signal = {});

  struct ImageWithMemory {
    vkhpp::UniqueImage image;
    vkhpp::UniqueDeviceMemory imageMemory;
    vkhpp::UniqueImageView imageView;
  };
  Result<ImageWithMemory> CreateImage(
      uint32_t width, uint32_t height, vkhpp::Format format,
      vkhpp::ImageUsageFlags usages,
      vkhpp::MemoryPropertyFlags memory_properties,
      vkhpp::ImageLayout returned_layout);

  Result<Ok> LoadImage(const vkhpp::UniqueImage& image, uint32_t width,
                       uint32_t height, const std::vector<uint8_t>& imageData,
                       vkhpp::ImageLayout currentLayout,
                       vkhpp::ImageLayout returnedLayout);

  Result<std::vector<uint8_t>> DownloadImage(
      uint32_t width, uint32_t height, const vkhpp::UniqueImage& image,
      vkhpp::ImageLayout current_layout, vkhpp::ImageLayout returned_layout);

  struct YuvImageWithMemory {
    vkhpp::UniqueSamplerYcbcrConversion imageSamplerConversion;
    vkhpp::UniqueSampler imageSampler;
    vkhpp::UniqueDeviceMemory imageMemory;
    vkhpp::UniqueImage image;
    vkhpp::UniqueImageView imageView;
  };
  Result<YuvImageWithMemory> CreateYuvImage(
      uint32_t width, uint32_t height, vkhpp::ImageUsageFlags usages,
      vkhpp::MemoryPropertyFlags memory_properties,
      vkhpp::ImageLayout returned_layout);

  Result<Ok> LoadYuvImage(const vkhpp::UniqueImage& image, uint32_t width,
                          uint32_t height,
                          const std::vector<uint8_t>& image_data_y,
                          const std::vector<uint8_t>& image_data_u,
                          const std::vector<uint8_t>& image_data_v,
                          vkhpp::ImageLayout current_layout,
                          vkhpp::ImageLayout returned_layout);

  struct FramebufferWithAttachments {
    std::optional<ImageWithMemory> colorAttachment;
    std::optional<ImageWithMemory> depthAttachment;
    vkhpp::UniqueRenderPass renderpass;
    vkhpp::UniqueFramebuffer framebuffer;
  };
  Result<FramebufferWithAttachments> CreateFramebuffer(
      uint32_t width, uint32_t height,
      vkhpp::Format colorAttachmentFormat = vkhpp::Format::eUndefined,
      vkhpp::Format depthAttachmentFormat = vkhpp::Format::eUndefined);

 private:
  vkhpp::DynamicLoader mLoader;
  vkhpp::UniqueInstance mInstance;
  std::optional<vkhpp::UniqueDebugUtilsMessengerEXT> mDebugMessenger;

 protected:
  vkhpp::PhysicalDevice mPhysicalDevice;
  vkhpp::UniqueDevice mDevice;
  vkhpp::Queue mQueue;
  uint32_t mQueueFamilyIndex = 0;

 private:
  static constexpr const VkDeviceSize kStagingBufferSize = 32 * 1024 * 1024;
  BufferWithMemory mStagingBuffer;

  struct PerFrameObjects {
    vkhpp::UniqueFence readyFence;
    vkhpp::UniqueSemaphore readyForRender;
    vkhpp::UniqueSemaphore readyForPresent;
    vkhpp::UniqueCommandPool commandPool;
    vkhpp::UniqueCommandBuffer commandBuffer;
  };
  static constexpr const uint32_t kMaxFramesInFlight = 3;
  uint32_t mCurrentFrame = 0;
  std::vector<PerFrameObjects> mFrameObjects;

  ANativeWindow* mWindow = nullptr;

  std::optional<vkhpp::SurfaceKHR> mSurface;

  struct SwapchainObjects {
    vkhpp::SurfaceFormatKHR swapchainFormat;
    vkhpp::Extent2D swapchainExtent;
    vkhpp::UniqueSwapchainKHR swapchain;
    std::vector<vkhpp::Image> swapchainImages;
    std::vector<vkhpp::UniqueImageView> swapchainImageViews;
  };
  std::optional<SwapchainObjects> mSwapchainObjects;
};

Result<std::unique_ptr<SampleBase>> BuildVulkanSampleApp();

}  // namespace cuttlefish
