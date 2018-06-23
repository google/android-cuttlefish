/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "host/commands/launch/vsoc_shared_memory.h"

#include <unistd.h>

#include <map>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/size_utils.h"
#include "common/vsoc/lib/vsoc_memory.h"
#include "glog/logging.h"

#include "uapi/vsoc_shm.h"

namespace vsoc {

namespace {

uint32_t OffsetOfRegionData(const VSoCRegionLayout& layout) {
  uint32_t offset = 0;
  // Signal tables
  offset +=
      (1 << layout.guest_to_host_signal_table_log_size()) * sizeof(uint32_t);
  offset +=
      (1 << layout.host_to_guest_signal_table_log_size()) * sizeof(uint32_t);
  // Interrup signals
  offset += 2 * sizeof(uint32_t);
  return offset;
}

struct VSoCRegionAllocator {
  const VSoCRegionLayout* region_layout;
  uint32_t begin_offset;
  uint32_t region_size;

  VSoCRegionAllocator(const VSoCRegionLayout& layout, uint32_t offset,
                      uint32_t requested_layout_increase = 0)
      : region_layout(&layout),
        begin_offset(offset),
        region_size(cvd::AlignToPageSize(OffsetOfRegionData(layout) +
                                         layout.layout_size() +
                                         requested_layout_increase)) {}
};

// Writes a region's signal table layout to shared memory. Returns the region
// offset of free memory after the table and interrupt signaled word.
uint32_t WriteSignalTableDescription(vsoc_signal_table_layout* layout,
                                     uint32_t offset, int log_size) {
  layout->num_nodes_lg2 = log_size;
  // First the signal table
  layout->futex_uaddr_table_offset = offset;
  offset += (1 << log_size) * sizeof(uint32_t);
  // Then the interrupt signaled word
  layout->interrupt_signalled_offset = offset;
  offset += sizeof(uint32_t);
  return offset;
}

// Writes a region's layout description to shared memory
void WriteRegionDescription(vsoc_device_region* shmem_region_desc,
                            const VSoCRegionAllocator& allocator) {
  // Region versions are deprecated, write some sensible value
  shmem_region_desc->current_version = 0;
  shmem_region_desc->min_compatible_version = 0;

  shmem_region_desc->region_begin_offset = allocator.begin_offset;
  shmem_region_desc->region_end_offset =
      allocator.begin_offset + allocator.region_size;
  shmem_region_desc->offset_of_region_data =
      OffsetOfRegionData(*allocator.region_layout);
  strncpy(shmem_region_desc->device_name,
          allocator.region_layout->region_name(), VSOC_DEVICE_NAME_SZ - 1);
  shmem_region_desc->device_name[VSOC_DEVICE_NAME_SZ - 1] = '\0';
  // Guest to host signal table at the beginning of the region
  uint32_t offset = 0;
  offset = WriteSignalTableDescription(
      &shmem_region_desc->guest_to_host_signal_table, offset,
      allocator.region_layout->guest_to_host_signal_table_log_size());
  // Host to guest signal table right after
  offset = WriteSignalTableDescription(
      &shmem_region_desc->host_to_guest_signal_table, offset,
      allocator.region_layout->host_to_guest_signal_table_log_size());
  // Double check that the region metadata does not collide with the data
  if (offset > shmem_region_desc->offset_of_region_data) {
    LOG(FATAL) << "Error: Offset of region data too small (is "
               << shmem_region_desc->offset_of_region_data << " should be "
               << offset << " ) for region "
               << allocator.region_layout->region_name() << ". This is a bug";
  }
}

void WriteLayout(void* shared_memory,
                 const std::vector<VSoCRegionAllocator>& allocators,
                 uint32_t file_size) {
  // Device header
  static_assert(CURRENT_VSOC_LAYOUT_MAJOR_VERSION == 2,
                "Region layout code must be updated");
  auto header = reinterpret_cast<vsoc_shm_layout_descriptor*>(shared_memory);
  header->major_version = CURRENT_VSOC_LAYOUT_MAJOR_VERSION;
  header->minor_version = CURRENT_VSOC_LAYOUT_MINOR_VERSION;
  header->size = file_size;
  header->region_count = allocators.size();

  std::map<std::string, size_t> region_idx_by_name;
  for (size_t idx = 0; idx < allocators.size(); ++idx) {
    region_idx_by_name[allocators[idx].region_layout->region_name()] = idx;
  }

  // Region descriptions go right after the layout descriptor
  header->vsoc_region_desc_offset = sizeof(vsoc_shm_layout_descriptor);
  auto region_descriptions = reinterpret_cast<vsoc_device_region*>(header + 1);
  for (size_t idx = 0; idx < allocators.size(); ++idx) {
    auto shmem_region_desc = &region_descriptions[idx];
    const auto& region = *allocators[idx].region_layout;
    WriteRegionDescription(shmem_region_desc, allocators[idx]);
    // Handle managed_by links
    if (region.managed_by()) {
      auto manager_idx = region_idx_by_name.at(region.managed_by());
      if (manager_idx == VSOC_REGION_WHOLE) {
        LOG(FATAL) << "Region '" << region.region_name() << "' has owner "
                   << region.managed_by() << " with index " << manager_idx
                   << " which is the default value for regions without an "
                      "owner. Choose a different region to be at index "
                   << manager_idx
                   << ", make sure the chosen region is NOT the owner of any "
                      "other region";
      }
      shmem_region_desc->managed_by = manager_idx;
    } else {
      shmem_region_desc->managed_by = VSOC_REGION_WHOLE;
    }
  }
}
}  // namespace

void CreateSharedMemoryFile(
    const std::string& path,
    const std::map<std::string, uint32_t>& layout_increases) {
  // TODO(ender): Lock the file after creation and check lock status upon second
  // execution attempt instead of throwing an error.
  LOG_IF(WARNING, unlink(path.c_str()) == 0)
      << "Removed existing instance of " << path
      << ". We currently don't know if another instance of daemon is running";
  auto shared_mem_fd = cvd::SharedFD::Open(
      path.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  LOG_IF(FATAL, !shared_mem_fd->IsOpen())
      << "Error in creating shared_memory file: " << shared_mem_fd->StrError();

  auto region_layouts = VSoCMemoryLayout::Get()->GetRegions();
  std::vector<VSoCRegionAllocator> allocators;
  uint32_t file_size =
      cvd::AlignToPageSize(sizeof(vsoc_shm_layout_descriptor) +
                           region_layouts.size() * sizeof(vsoc_device_region));
  for (auto layout : region_layouts) {
    allocators.emplace_back(*layout, file_size /* offset */,
                            layout_increases.count(layout->region_name())
                                ? layout_increases.at(layout->region_name())
                                : 0);
    file_size += allocators.back().region_size;
  }
  file_size = cvd::RoundUpToNextPowerOf2(file_size);

  int truncate_res = shared_mem_fd->Truncate(file_size);
  LOG_IF(FATAL, truncate_res == -1)
      << "Error in sizing up the shared memory file: "
      << shared_mem_fd->StrError();

  void* mmap_addr =
      shared_mem_fd->Mmap(0, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, 0);
  LOG_IF(FATAL, mmap_addr == MAP_FAILED)
      << "Error mmaping file: " << strerror(errno);
  WriteLayout(mmap_addr, allocators, file_size);
  munmap(mmap_addr, file_size);
}

}  // namespace vsoc
