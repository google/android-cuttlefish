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

#include "common/vsoc/lib/vsoc_memory.h"
#include "uapi/vsoc_shm.h"

namespace ivserver {
namespace {

class VSoCSharedMemoryImpl : public VSoCSharedMemory {
 public:
  VSoCSharedMemoryImpl(const std::map<std::string, size_t> &name_to_region_idx,
                       const std::vector<Region> &regions,
                       const std::string &path);

  bool GetEventFdPairForRegion(const std::string &region_name,
                               cvd::SharedFD *guest_to_host,
                               cvd::SharedFD *host_to_guest) const override;

  const cvd::SharedFD &SharedMemFD() const override;

  const std::vector<Region> &Regions() const override;

 private:
  void CreateLayout();

  cvd::SharedFD shared_mem_fd_;
  const std::map<std::string, size_t> region_name_to_index_;
  const std::vector<Region> region_data_;

  VSoCSharedMemoryImpl(const VSoCSharedMemoryImpl &) = delete;
  VSoCSharedMemoryImpl &operator=(const VSoCSharedMemoryImpl &other) = delete;
};

VSoCSharedMemoryImpl::VSoCSharedMemoryImpl(
    const std::map<std::string, size_t> &name_to_region_idx,
    const std::vector<Region> &regions, const std::string &path)
    : shared_mem_fd_(cvd::SharedFD::Open(path.c_str(), O_RDWR)),
      region_name_to_index_{name_to_region_idx},
      region_data_{regions} {
  LOG_IF(FATAL, !shared_mem_fd_->IsOpen())
      << "Error in creating shared_memory file: " << shared_mem_fd_->StrError();
}

const cvd::SharedFD &VSoCSharedMemoryImpl::SharedMemFD() const {
  return shared_mem_fd_;
}

const std::vector<VSoCSharedMemory::Region> &VSoCSharedMemoryImpl::Regions()
    const {
  return region_data_;
}

bool VSoCSharedMemoryImpl::GetEventFdPairForRegion(
    const std::string &region_name, cvd::SharedFD *guest_to_host,
    cvd::SharedFD *host_to_guest) const {
  auto it = region_name_to_index_.find(region_name);
  if (it == region_name_to_index_.end()) return false;

  *guest_to_host = region_data_[it->second].host_fd;
  *host_to_guest = region_data_[it->second].guest_fd;
  return true;
}

}  // anonymous namespace

std::unique_ptr<VSoCSharedMemory> VSoCSharedMemory::New(
    const std::string &path) {
  auto device_layout = vsoc::VSoCMemoryLayout::Get();

  std::map<std::string, size_t> name_to_region_idx;
  std::vector<Region> regions;
  regions.reserve(device_layout->GetRegions().size());

  for (auto region_spec : device_layout->GetRegions()) {
    auto device_name = region_spec->region_name();

    // Create one pair of eventfds for this region. Note that the guest to host
    // eventfd is non-blocking, whereas the host to guest eventfd is blocking.
    // This is in anticipation of blocking semantics for the host side locks.
    auto host_fd = cvd::SharedFD::Event(0, EFD_NONBLOCK);
    if (!host_fd->IsOpen()) {
      LOG(ERROR) << "Failed to create host eventfd for " << device_name << ": "
                 << host_fd->StrError();
      return nullptr;
    }
    auto guest_fd = cvd::SharedFD::Event(0, EFD_NONBLOCK);
    if (!guest_fd->IsOpen()) {
      LOG(ERROR) << "Failed to create guest eventfd for " << device_name << ": "
                 << guest_fd->StrError();
      return nullptr;
    }

    auto region_idx = regions.size();
    name_to_region_idx[device_name] = region_idx;
    regions.emplace_back(device_name, host_fd, guest_fd);
  }

  return std::unique_ptr<VSoCSharedMemory>(
      new VSoCSharedMemoryImpl(name_to_region_idx, regions, path));
}

}  // namespace ivserver
