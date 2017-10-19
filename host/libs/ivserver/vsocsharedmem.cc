/*
 * Copyright (C) 2017 The Android Open Source Project
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
#include "host/libs/ivserver/vsocsharedmem.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <tuple>

#include <glog/logging.h>

#include "uapi/vsoc_shm.h"

namespace ivserver {
namespace {

static_assert(CURRENT_VSOC_LAYOUT_MAJOR_VERSION == 2,
              "Region layout code must be updated");

// Field names from the json file. These are declared so the compiler will
// catch typos.
const char g_vsoc_device_regions[] = "vsoc_device_regions";
const char g_device_name_tag[] = "device_name";
const char g_managed_by_tag[] = "managed_by";

class RegionAllocator {
 public:
  static uint32_t PageSize() {
    static long page_size = sysconf(_SC_PAGESIZE);
    return page_size;
  }

  explicit RegionAllocator(const std::string &name, uint32_t max_size,
                           uint32_t offset = 0)
      : name_{name}, max_size_{max_size}, offset_{offset} {}

  uint32_t Allocate(uint32_t size, const char *usage, bool *error) {
    if (size > (max_size_ - offset_)) {
      *error = true;
      LOG(ERROR) << name_ << ":"
                 << "allocation of " << size << "bytes for " << usage
                 << " will overflow memory region";
    }

    offset_ += size;
    return (offset_ - size);
  }

  void PadTo(uint32_t size, bool *error) {
    uint32_t padding = ((offset_ + (size - 1)) / size) * size - offset_;
    Allocate(padding, "padding", error);
  }

  uint32_t AllocateRest(bool *error) {
    return Allocate(max_size_ - offset_, "rest of region", error);
  }

  uint32_t GetOffset() const { return offset_; }

 private:
  std::string name_;
  uint32_t max_size_;
  uint32_t offset_;
};

class VSoCSharedMemoryImpl : public VSoCSharedMemory {
 public:
  VSoCSharedMemoryImpl(const vsoc_shm_layout_descriptor &header,
                       const std::map<std::string, size_t> &name_to_region_idx,
                       const std::vector<Region> &regions,
                       const std::string &path);

  bool GetEventFdPairForRegion(const std::string &region_name,
                               avd::SharedFD *guest_to_host,
                               avd::SharedFD *host_to_guest) const override;

  const avd::SharedFD &SharedMemFD() const override;

  const std::vector<Region> &Regions() const override;

 private:
  void CreateLayout();

  const vsoc_shm_layout_descriptor &header_;
  avd::SharedFD shared_mem_fd_;
  const std::map<std::string, size_t> region_name_to_index_;
  const std::vector<Region> region_data_;

  VSoCSharedMemoryImpl(const VSoCSharedMemoryImpl &) = delete;
  VSoCSharedMemoryImpl &operator=(const VSoCSharedMemoryImpl &other) = delete;
};

VSoCSharedMemoryImpl::VSoCSharedMemoryImpl(
    const vsoc_shm_layout_descriptor &header,
    const std::map<std::string, size_t> &name_to_region_idx,
    const std::vector<Region> &regions, const std::string &path)
    : header_{header},
      region_name_to_index_{name_to_region_idx},
      region_data_{regions} {
  // TODO(ender): Lock the file after creation and check lock status upon second
  // execution attempt instead of throwing an error.
  LOG_IF(WARNING, unlink(path.c_str()) == 0)
      << "Removed existing instance of " << path
      << ". We currently don't know if another instance of daemon is running";
  shared_mem_fd_ = avd::SharedFD::Open(path.c_str(), O_RDWR | O_CREAT | O_EXCL,
                                       S_IRUSR | S_IWUSR);
  LOG_IF(FATAL, !shared_mem_fd_->IsOpen())
      << "Error in creating shared_memory file: " << shared_mem_fd_->StrError();

  int truncate_res = shared_mem_fd_->Truncate(header_.size);
  LOG_IF(FATAL, truncate_res == -1)
      << "Error in sizing up the shared memory file: "
      << shared_mem_fd_->StrError();
  CreateLayout();
}

const avd::SharedFD &VSoCSharedMemoryImpl::SharedMemFD() const {
  return shared_mem_fd_;
}

const std::vector<VSoCSharedMemory::Region> &VSoCSharedMemoryImpl::Regions()
    const {
  return region_data_;
}

void VSoCSharedMemoryImpl::CreateLayout() {
  void *mmap_addr = shared_mem_fd_->Mmap(0, header_.size,
                                         PROT_READ | PROT_WRITE, MAP_SHARED, 0);
  LOG_IF(FATAL, mmap_addr == MAP_FAILED)
      << "Error mmaping file: " << strerror(errno);
  *reinterpret_cast<vsoc_shm_layout_descriptor *>(mmap_addr) = header_;
  auto region_dest = reinterpret_cast<vsoc_device_region *>(
      reinterpret_cast<uintptr_t>(mmap_addr) + header_.vsoc_region_desc_offset);

  for (const auto &region : region_data_) {
    *region_dest++ = region.values;
  }
  munmap(mmap_addr, header_.size);
}

bool VSoCSharedMemoryImpl::GetEventFdPairForRegion(
    const std::string &region_name, avd::SharedFD *guest_to_host,
    avd::SharedFD *host_to_guest) const {
  auto it = region_name_to_index_.find(region_name);
  if (it == region_name_to_index_.end()) return false;

  *guest_to_host = region_data_[it->second].host_fd;
  *host_to_guest = region_data_[it->second].guest_fd;
  return true;
}

template <typename T>
void GetMandatoryUInt(const std::string &region_name,
                      const Json::Value &json_region, const char *field_name,
                      bool *error, T *dest) {
  if (!json_region.isMember(field_name)) {
    LOG(ERROR) << region_name << " missing " << field_name << " field";
    *error = true;
    return;
  }
  if (!json_region[field_name].isNumeric()) {
    LOG(ERROR) << region_name << " " << field_name << " is not numeric";
    *error = true;
    return;
  }
  *dest = json_region[field_name].asUInt();
}

void JSONToSignalTable(const std::string &region_name,
                       const Json::Value &json_region, const char *table_name,
                       RegionAllocator *allocator, bool *error,
                       vsoc_signal_table_layout *dest) {
  if (!json_region.isMember(table_name)) {
    LOG(ERROR) << region_name << " has no " << table_name << " section";
    *error = true;
    return;
  }
  GetMandatoryUInt(region_name, json_region[table_name], "num_nodes_lg2", error,
                   &dest->num_nodes_lg2);
  dest->futex_uaddr_table_offset = allocator->Allocate(
      (1 << dest->num_nodes_lg2) * sizeof(uint32_t), "node table", error);
  dest->interrupt_signalled_offset =
      allocator->Allocate(sizeof(uint32_t), "signal word", error);
}

std::shared_ptr<VSoCSharedMemory::Region> JSONToRegion(
    const std::string &region_name, const Json::Value &json_region,
    bool *failed) {
  std::shared_ptr<VSoCSharedMemory::Region> region(
      new VSoCSharedMemory::Region);
  GetMandatoryUInt(region_name, json_region, "current_version", failed,
                   &region->values.current_version);
  GetMandatoryUInt(region_name, json_region, "min_compatible_version", failed,
                   &region->values.min_compatible_version);
  GetMandatoryUInt(region_name, json_region, "region_size", failed,
                   &region->values.region_end_offset);
  RegionAllocator allocator(region_name, region->values.region_end_offset);
  JSONToSignalTable(region_name, json_region, "guest_to_host_signal_table",
                    &allocator, failed,
                    &region->values.guest_to_host_signal_table);
  JSONToSignalTable(region_name, json_region, "host_to_guest_signal_table",
                    &allocator, failed,
                    &region->values.host_to_guest_signal_table);

  region->values.offset_of_region_data = allocator.AllocateRest(failed);
  return region;
}

}  // anonymous namespace

std::unique_ptr<VSoCSharedMemory> VSoCSharedMemory::New(
    const std::string &path, const Json::Value &root) {
  // This is so catastrophic that there isn't anything else to check.
  if (!root.isMember(g_vsoc_device_regions)) {
    LOG(ERROR) << "vsoc_device_regions section is absent";
    return nullptr;
  }
  auto device_regions = root[g_vsoc_device_regions];
  bool failed = false;
  RegionAllocator shm_file("shared_memory_file", UINT32_MAX);
  vsoc_shm_layout_descriptor header{};
  header.major_version = CURRENT_VSOC_LAYOUT_MAJOR_VERSION;
  header.minor_version = CURRENT_VSOC_LAYOUT_MINOR_VERSION;
  // size is handled at N1
  header.region_count = device_regions.size();
  shm_file.Allocate(sizeof(header), "header", &failed);
  header.vsoc_region_desc_offset =
      shm_file.Allocate(sizeof(vsoc_device_region) * header.region_count,
                        "region descriptors", &failed);
  // Align to a page boundary for the first region
  shm_file.PadTo(RegionAllocator::PageSize(), &failed);

  std::map<std::string, size_t> name_to_region_idx;
  std::vector<Region> regions;
  regions.reserve(header.region_count);
  std::map<std::string, std::string> managed_by_references;

  // Pass 1: Parse individual region structures validating all of the
  // fields that can be validated without help.
  for (const auto &json_region : device_regions) {
    if (!json_region.isMember(g_device_name_tag)) {
      LOG(ERROR) << g_device_name_tag << " is missing from region";
      failed = true;
      continue;
    }
    const std::string device_name = json_region[g_device_name_tag].asString();
    if (name_to_region_idx.count(device_name)) {
      LOG(ERROR) << device_name << " used for more than one region";
      failed = true;
      continue;
    }
    std::shared_ptr<Region> region =
        JSONToRegion(device_name, json_region, &failed);
    // Create one pair of eventfds for this region. Note that the guest to host
    // eventfd is non-blocking, whereas the host to guest eventfd is blocking.
    // This is in anticipation of blocking semantics for the host side locks.
    region->host_fd = avd::SharedFD::Event(0, EFD_NONBLOCK);
    if (!region->host_fd->IsOpen()) {
      failed = true;
      LOG(ERROR) << "Failed to create host eventfd for " << device_name << ": "
                 << region->host_fd->StrError();
    }
    region->guest_fd = avd::SharedFD::Event(0, EFD_NONBLOCK);
    if (!region->guest_fd->IsOpen()) {
      failed = true;
      LOG(ERROR) << "Failed to create guest eventfd for " << device_name << ": "
                 << region->guest_fd->StrError();
    }
    region->values.region_begin_offset = shm_file.Allocate(
        region->values.region_end_offset, device_name.c_str(), &failed);
    shm_file.PadTo(RegionAllocator::PageSize(), &failed);
    region->values.region_end_offset += region->values.region_begin_offset;
    if (sizeof(region->values.device_name) - device_name.size() < 1) {
      LOG(ERROR) << device_name << " is too long for a region name";
      failed = true;
    } else {
      strcpy(region->values.device_name, device_name.c_str());
    }

    name_to_region_idx[device_name] = regions.size();
    regions.push_back(*region);
    // We will attempt to resolve this link in Pass 2
    if (json_region.isMember(g_managed_by_tag)) {
      managed_by_references[device_name] =
          json_region[g_managed_by_tag].asString();
    }
  }

  // Pass 2: Resolve the managed_by_references
  for (const auto &it : managed_by_references) {
    if (!name_to_region_idx.count(it.second)) {
      LOG(ERROR) << it.first << " managed by missing region " << it.second;
      failed = true;
      continue;
    }
    regions[name_to_region_idx[it.first]].values.managed_by =
        name_to_region_idx[it.second];
    if (regions[name_to_region_idx[it.first]].values.managed_by ==
        VSOC_REGION_WHOLE) {
      LOG(ERROR)
          << "Region '" << it.first << "' has owner " << it.second
          << " with index "
          << regions[name_to_region_idx[it.first]].values.managed_by
          << " which is the default value for regions without an owner. Choose"
             "a different region to be at index "
          << regions[name_to_region_idx[it.first]].values.managed_by
          << ", make sure the chosen region is NOT the owner of any other "
             "region";
    }
  }
  if (failed) {
    return nullptr;
  }
  // Handles size (marker N1)
  // The size must be a power of 2
  size_t temp = shm_file.GetOffset();
  size_t allocated_size = 0;
  // Find the highest set bit
  while(temp) {
    allocated_size = temp;
    // Clear a bit
    temp = temp & (temp -1);
  }
  // If the size is already a power of 2 just use it. Otherswise use the
  // next higher power of 2
  if (allocated_size < shm_file.GetOffset()) {
    allocated_size *= 2;
  }
  header.size = allocated_size;
  return std::unique_ptr<VSoCSharedMemory>(
      new VSoCSharedMemoryImpl(header, name_to_region_idx, regions, path));
}

}  // namespace ivserver
