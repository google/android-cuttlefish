#include "host/ivserver/vsocsharedmem.h"
#include "host/ivserver/layout.h"

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

#define LOG_TAG "ivserver::VSoCSharedMemory"

namespace ivserver {

VSoCSharedMemory::VSoCSharedMemory(const uint32_t &size_mib,
                                   const std::string &name,
                                   const Json::Value &json_root)
    : size_{size_mib << 20}, json_root_{json_root} {
  shmfd_ = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);

  if (shmfd_ == -1) {
    LOG(FATAL) << "Error in creating shared_memory file. Please remove "
                  " existing file and retry.";
    return;
  }

  if (ftruncate(shmfd_, size_) == -1) {
    LOG(FATAL) << "Error in sizing up the shared memory file.";
    close(shmfd_);
    shmfd_ = -1;
    return;
  }

  if (!CreateLayout()) {
    close(shmfd_);
    shmfd_ = -1;
    return;
  }

  initialized_ = true;
}

bool VSoCSharedMemory::CreateLayout() {
  uint32_t offset = 0;
  void *mmap_addr =
      mmap(0, size_, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd_, 0);
  if (mmap_addr == MAP_FAILED) {
    LOG(FATAL) << "Error in mmap.";
    return false;
  }

  vsoc_shm_layout_descriptor layout_descriptor;
  memset(&layout_descriptor, 0, sizeof(layout_descriptor));

  layout_descriptor.major_version = kLayoutVersionMajor;
  layout_descriptor.minor_version = kLayoutVersionMinor;
  layout_descriptor.size = size_;

  //
  // TODO(romitd): error checking and sanity.
  // TODO(romitd): Refactor
  //
  layout_descriptor.region_count =
      json_root_["vsoc_shm_layout_descriptor"]["region_count"].asUInt();

  layout_descriptor.vsoc_region_desc_offset =
      json_root_["vsoc_shm_layout_descriptor"]["vsoc_region_desc_offset"]
          .asUInt();

  *reinterpret_cast<vsoc_shm_layout_descriptor *>(
      reinterpret_cast<char *>(mmap_addr) + offset) = layout_descriptor;

  // Move to the region_descriptor area.
  offset += layout_descriptor.vsoc_region_desc_offset;

  for (const auto &region : json_root_["vsoc_device_regions"]) {
    vsoc_device_region device_region;
    memset(&device_region, sizeof(vsoc_device_region), 0);

    //
    // First populate the ones from JSON.
    //
    device_region.current_version = region["current_version"].asUInt();
    device_region.min_compatible_version =
        region["min_compatible_version"].asUInt();

    device_region.region_begin_offset = region["region_begin_offset"].asUInt();
    device_region.region_end_offset = region["region_end_offset"].asUInt();

    device_region.guest_to_host_signal_table.num_nodes_lg2 =
        region["guest_to_host_signal_table"]["num_nodes_lg2"].asUInt();

    device_region.host_to_guest_signal_table.num_nodes_lg2 =
        region["host_to_guest_signal_table"]["num_nodes_lg2"].asUInt();

    strncpy(device_region.device_name, region["device_name"].asCString(),
            sizeof(device_region.device_name) - 1);

    //
    // Now populate the derivatives.
    //
    device_region.guest_to_host_signal_table.offset =
        sizeof(vsoc_device_region);

    device_region.guest_to_host_signal_table.node_alloc_hint_offset =
        device_region.guest_to_host_signal_table.offset +
        (1 << device_region.guest_to_host_signal_table.num_nodes_lg2) *
            sizeof(int32_t);

    device_region.host_to_guest_signal_table.offset =
        device_region.guest_to_host_signal_table.node_alloc_hint_offset +
        sizeof(device_region.guest_to_host_signal_table.node_alloc_hint_offset);

    device_region.host_to_guest_signal_table.node_alloc_hint_offset =
        (1 << device_region.guest_to_host_signal_table.num_nodes_lg2) *
            sizeof(int32_t) +
        device_region.host_to_guest_signal_table.offset;

    device_region.offset_of_region_data =
        device_region.host_to_guest_signal_table.node_alloc_hint_offset +
        sizeof(device_region.host_to_guest_signal_table.node_alloc_hint_offset);

    *reinterpret_cast<vsoc_device_region *>(
        reinterpret_cast<char *>(mmap_addr) + offset) = device_region;
    offset += sizeof(vsoc_device_region);

    //
    // Create one pair of eventfds for this region. Note that the guest to host
    // eventfd is non-blocking, whereas the host to guest eventfd is blocking.
    // This is in anticipation of blocking semantics for the host side locks.
    //
    int g_to_h_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (g_to_h_efd == -1) {
      LOG(FATAL) << "Failed to create eventfd (guest to host).";
      return false;  // Probably superfluous.
    }

    int h_to_g_efd = eventfd(0, EFD_CLOEXEC);
    if (h_to_g_efd == -1) {
      LOG(FATAL) << "Failed to create eventfd (host to guest).";
      return false;  // Probably superfluous.
    }

    //
    // Store the eventfd data for the client queries.
    //
    eventfd_data_.push_back(std::make_tuple(region["device_name"].asString(),
                                            g_to_h_efd, h_to_g_efd));
  }

  munmap(mmap_addr, size_);

  return true;
}

bool VSoCSharedMemory::GetEventFDpairForRegion(const std::string &region_name,
                                               int *guest_to_host,
                                               int *host_to_guest) const {
  auto it =
      find_if(eventfd_data_.begin(), eventfd_data_.end(),
              [&region_name](const std::tuple<std::string, int, int> &param) {
                return region_name == std::get<0>(param);
              });
  if (it == eventfd_data_.end()) {
    *guest_to_host = -1;
    *host_to_guest = -1;
    return false;
  }

  *guest_to_host = std::get<1>(*it);
  *host_to_guest = std::get<2>(*it);
  return true;
}

}  // namespace ivserver
