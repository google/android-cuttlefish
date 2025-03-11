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

#include "cuttlefish/host/graphics_detector/graphics_detector_vk_external_memory_host.h"

#include "cuttlefish/host/graphics_detector/vulkan.h"

#if defined(__linux__)
#include <sys/mman.h>
#include <syscall.h>
#include <unistd.h>
#endif

namespace gfxstream {
namespace {

#if defined(__linux__)
class ScopedFd {
 public:
  explicit ScopedFd(int fd) { Reset(fd); }
  ~ScopedFd() { Reset(); }

  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;

  ScopedFd(ScopedFd&& rhs) {
    Reset(rhs.Release());
    rhs.mFd = -1;
  }

  ScopedFd& operator=(ScopedFd&& rhs) {
    Reset(rhs.Release());
    return *this;
  }

  int Get() const { return mFd; }

  int Release() {
    int localFd = mFd;
    mFd = -1;
    return localFd;
  }

  void* Map(size_t size) {
    if (mFd == -1) {
      return nullptr;
    }

    mMappedAddr =
        mmap(nullptr, size, PROT_WRITE | PROT_READ, MAP_SHARED, mFd, 0);
    if (mMappedAddr == nullptr) {
      return nullptr;
    }

    mMappedSize = size;
    return mMappedAddr;
  }

  void Unmap() {
    if (mMappedAddr != nullptr) {
      munmap(mMappedAddr, mMappedSize);
      mMappedAddr = nullptr;
      mMappedSize = 0;
    }
  }

 private:
  void Reset(int newFd = -1) {
    if (mFd != -1) {
      // Unmap();
      close(mFd);
    }
    mFd = newFd;
  }

  int mFd = -1;
  void* mMappedAddr = nullptr;
  size_t mMappedSize = 0;
};

gfxstream::expected<ScopedFd, std::string> CreateSharedMemory(
    vk::DeviceSize size) {
  int fd = -1;

#if !defined(ANDROID)
  fd = memfd_create("unused-name", MFD_CLOEXEC);
#elif defined(__NR_memfd_create)
  // Android host toolchain using a really old glibc ?_?
  fd = syscall(__NR_memfd_create, "unused-name", MFD_CLOEXEC);
#else
  return gfxstream::unexpected(
      "Failed to create shared memory: memfd_create unavailable.");
#endif

  if (fd == -1) {
    return gfxstream::unexpected("Failed to create shared memory: " +
                                 std::string(strerror(errno)));
  }
  if (ftruncate(fd, static_cast<off_t>(size))) {
    const std::string error = std::string(strerror(errno));
    close(fd);
    return gfxstream::unexpected("Failed to resize shared memory: " + error);
  }
  return ScopedFd(fd);
}

gfxstream::expected<Ok, std::string> CheckImportingSharedMemory(
    vk::PhysicalDevice& physicalDevice) {
  const vk::PhysicalDeviceMemoryProperties physicalDeviceMemoryProperties =
      physicalDevice.getMemoryProperties();

  const auto properties2 =
      physicalDevice
          .getProperties2<vk::PhysicalDeviceProperties2,  //
                          vk::PhysicalDeviceExternalMemoryHostPropertiesEXT>();

  const auto& physicalDeviceExternalMemoryHostProperties =
      properties2.get<vk::PhysicalDeviceExternalMemoryHostPropertiesEXT>();

  const float queuePriority = 1.0f;
  const vk::DeviceQueueCreateInfo deviceQueueCreateInfo = {
      .queueFamilyIndex = 0,
      .queueCount = 1,
      .pQueuePriorities = &queuePriority,
  };
  const std::vector<const char*> requestedDeviceExtensions = {
      VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
  };
  const vk::DeviceCreateInfo deviceCreateInfo = {
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &deviceQueueCreateInfo,
      .enabledExtensionCount =
          static_cast<uint32_t>(requestedDeviceExtensions.size()),
      .ppEnabledExtensionNames = requestedDeviceExtensions.data(),
  };
  auto device = VK_EXPECT_RV_OR_STRING(
      physicalDevice.createDeviceUnique(deviceCreateInfo));

  constexpr const vk::DeviceSize kSize = 16384;

  // TODO: check alignment
  (void)physicalDeviceExternalMemoryHostProperties;

  auto shm = GFXSTREAM_EXPECT(CreateSharedMemory(kSize));

  void* mappedShm = shm.Map(kSize);
  if (mappedShm == MAP_FAILED) {
    return gfxstream::unexpected("Failed to mmap shared memory: " +
                                 std::string(strerror(errno)));
  }

  const vk::MemoryHostPointerPropertiesEXT memoryHostPointerProps =
      VK_EXPECT_RV_OR_STRING(device->getMemoryHostPointerPropertiesEXT(
          vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT, mappedShm));

  uint32_t memoryTypeIndex = -1;
  for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount;
       i++) {
    if (memoryHostPointerProps.memoryTypeBits & (1 << i)) {
      memoryTypeIndex = i;
      break;
    }
  }
  if (memoryTypeIndex == -1) {
    return gfxstream::unexpected(
        "Failed to find memory type compatible with shm.");
  }

  const vk::ImportMemoryHostPointerInfoEXT memoryHostPointerInto = {
      .handleType = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT,
      .pHostPointer = mappedShm,
  };

  const vk::MemoryAllocateInfo memoryAllocateInfo = {
      .pNext = &memoryHostPointerInto,
      .allocationSize = kSize,
      .memoryTypeIndex = memoryTypeIndex,
  };

  auto memory =
      VK_EXPECT_RV_OR_STRING(device->allocateMemoryUnique(memoryAllocateInfo));
  return Ok{};
}
#endif  // defined(__linux__)

gfxstream::expected<Ok, vk::Result> PopulateVulkanExternalMemoryHostQuirkImpl(
    ::gfxstream::proto::GraphicsAvailability* availability) {
  auto vk = GFXSTREAM_EXPECT(Vk::Load());

  ::gfxstream::proto::VulkanAvailability* vulkanAvailability =
      availability->mutable_vulkan();

  auto physicalDevices = VK_EXPECT_RV(vk.instance().enumeratePhysicalDevices());
  for (uint32_t i = 0; i < physicalDevices.size(); i++) {
    auto& physicalDevice = physicalDevices[i];
    auto* outPhysicalDevice = vulkanAvailability->mutable_physical_devices(i);

    const auto exts =
        VK_EXPECT_RV(physicalDevice.enumerateDeviceExtensionProperties());

    std::unordered_set<std::string> extensions;
    for (const auto& ext : exts) {
      extensions.insert(std::string(ext.extensionName));
    }
    if (extensions.find(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME) ==
        extensions.end()) {
      continue;
    }

    auto* quirks = outPhysicalDevice->mutable_quirks()
                       ->mutable_external_memory_host_quirks();

#if defined(__linux__)
    const auto shmResult = CheckImportingSharedMemory(physicalDevice);
    if (shmResult.ok()) {
      quirks->set_can_import_shm(true);
    } else {
      quirks->add_errors("can_import_shm error: " + shmResult.error());
      quirks->set_can_import_shm(false);
    }
#endif  // defined(__linux__)
  }

  return Ok{};
}

}  // namespace

gfxstream::expected<Ok, std::string> PopulateVulkanExternalMemoryHostQuirk(
    ::gfxstream::proto::GraphicsAvailability* availability) {
  return PopulateVulkanExternalMemoryHostQuirkImpl(availability)
      .transform_error([](vk::Result r) { return vk::to_string(r); });
}

}  // namespace gfxstream
