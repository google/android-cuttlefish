#include "host/ivserver/vsocsharedmem.h"

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

#include "host/ivserver/layout.h"
#include "host/ivserver/socketutils.h"

namespace ivserver {
namespace {
const uint16_t kLayoutVersionMajor = 1;
const uint16_t kLayoutVersionMinor = 0;
}  // anonymous namespace

VSoCSharedMemory::VSoCSharedMemory(const uint32_t &size_mib,
                                   const std::string &name,
                                   const Json::Value &json_root)
    : size_{size_mib << 20}, json_root_{json_root} {
  shmfd_ = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  LOG_IF(FATAL, shmfd_ == -1)
      << "Error in creating shared_memory file: " << strerror(errno);

  int trunc_res = ftruncate(shmfd_, size_);
  LOG_IF(FATAL, trunc_res == -1)
      << "Error in sizing up the shared memory file: " << strerror(errno);

  CreateLayout();
}

void VSoCSharedMemory::CreateLayout() {
  uint32_t offset = 0;
  void *mmap_addr =
      mmap(0, size_, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd_, 0);
  LOG_IF(FATAL, mmap_addr == MAP_FAILED)
      << "Error mmaping file: " << strerror(errno);

  vsoc_shm_layout_descriptor layout_descriptor;
  memset(&layout_descriptor, 0, sizeof(layout_descriptor));

  layout_descriptor.major_version = kLayoutVersionMajor;
  layout_descriptor.minor_version = kLayoutVersionMinor;
  layout_descriptor.size = size_;

  // TODO(romitd): error checking and sanity.
  // TODO(romitd): Refactor
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

    // Create one pair of eventfds for this region. Note that the guest to host
    // eventfd is non-blocking, whereas the host to guest eventfd is blocking.
    // This is in anticipation of blocking semantics for the host side locks.
    int g_to_h_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    LOG_IF(FATAL, g_to_h_efd == -1)
        << "Failed to create eventfd (guest to host): " << strerror(errno);
    int h_to_g_efd = eventfd(0, EFD_CLOEXEC);
    LOG_IF(FATAL, h_to_g_efd == -1)
        << "Failed to create eventfd (host to guest): " << strerror(errno);

    eventfd_data_.emplace(region["device_name"].asString(),
                          std::pair<int, int>{g_to_h_efd, h_to_g_efd});
  }

  munmap(mmap_addr, size_);
}

bool VSoCSharedMemory::GetEventFdPairForRegion(const std::string &region_name,
                                               int *guest_to_host,
                                               int *host_to_guest) const {
  auto it = eventfd_data_.find(region_name);
  if (it == eventfd_data_.end()) return false;

  *guest_to_host = it->second.first;
  *host_to_guest = it->second.second;
  return true;
}

void VSoCSharedMemory::BroadcastQemuSocket(int socket) const {
  for (const auto it : eventfd_data_) {
    int result;
    result = send_msg(socket, it.second.first, 0);
    LOG_IF(ERROR, result == -1) << "Failed to send QEmu socket to " << it.first
                                << " host: " << strerror(errno);
    result = send_msg(socket, it.second.second, 0);
    LOG_IF(ERROR, result == -1) << "Failed to send QEmu socket to " << it.first
                                << " guest: " << strerror(errno);
  }
}

}  // namespace ivserver
