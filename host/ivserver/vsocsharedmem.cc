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

#include "uapi/vsoc_shm.h"

namespace ivserver {
namespace {
const uint16_t kLayoutVersionMajor = 1;
const uint16_t kLayoutVersionMinor = 0;

class VSoCSharedMemoryImpl : public VSoCSharedMemory {
 public:
  VSoCSharedMemoryImpl(const uint32_t size_mib, const std::string &name,
                       const Json::Value &json_root);

  bool GetEventFdPairForRegion(const std::string &region_name,
                               avd::SharedFD *guest_to_host,
                               avd::SharedFD *host_to_guest) const override;

  const avd::SharedFD &SharedMemFD() const override;

  const std::map<std::string, Region> &Regions() const override;

 private:
  void CreateLayout();

  const uint32_t size_;
  const Json::Value &json_root_;
  avd::SharedFD shared_mem_fd_;
  std::map<std::string, Region> eventfd_data_;

  VSoCSharedMemoryImpl(const VSoCSharedMemoryImpl &) = delete;
  VSoCSharedMemoryImpl &operator=(const VSoCSharedMemoryImpl &other) = delete;
};

VSoCSharedMemoryImpl::VSoCSharedMemoryImpl(const uint32_t size_mib,
                                           const std::string &name,
                                           const Json::Value &json_root)
    : size_{size_mib << 20}, json_root_{json_root} {
  // TODO(ender): Lock the file after creation and check lock status upon second
  // execution attempt instead of throwing an error.
  LOG_IF(WARNING, unlink(name.c_str()) == 0)
      << "Removed existing instance of " << name
      << ". We currently don't know if another instance of daemon is running";
  shared_mem_fd_ = avd::SharedFD::Open(name.c_str(), O_RDWR | O_CREAT | O_EXCL,
                                       S_IRUSR | S_IWUSR);
  LOG_IF(FATAL, !shared_mem_fd_->IsOpen())
      << "Error in creating shared_memory file: " << shared_mem_fd_->StrError();

  int truncate_res = shared_mem_fd_->Truncate(size_);
  LOG_IF(FATAL, truncate_res == -1)
      << "Error in sizing up the shared memory file: "
      << shared_mem_fd_->StrError();

  CreateLayout();
}

const avd::SharedFD &VSoCSharedMemoryImpl::SharedMemFD() const {
  return shared_mem_fd_;
}

const std::map<std::string, VSoCSharedMemory::Region>
    &VSoCSharedMemoryImpl::Regions() const {
  return eventfd_data_;
}

void VSoCSharedMemoryImpl::CreateLayout() {
  uint32_t offset = 0;
  void *mmap_addr =
      shared_mem_fd_->Mmap(0, size_, PROT_READ | PROT_WRITE, MAP_SHARED, 0);
  LOG_IF(FATAL, mmap_addr == MAP_FAILED)
      << "Error mmaping file: " << strerror(errno);

  vsoc_shm_layout_descriptor layout_descriptor;
  memset(&layout_descriptor, 0, sizeof(layout_descriptor));

  layout_descriptor.major_version = kLayoutVersionMajor;
  layout_descriptor.minor_version = kLayoutVersionMinor;
  layout_descriptor.size = size_;

  // TODO(romitd): error checking and sanity.
  // TODO(romitd): Refactor
  layout_descriptor.region_count = json_root_["vsoc_device_regions"].size();

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

    const std::string &device_name = region["device_name"].asString();
    strncpy(device_region.device_name, device_name.c_str(),
            sizeof(device_region.device_name) - 1);

    device_region.guest_to_host_signal_table.offset_to_signal_table =
        sizeof(vsoc_device_region);

    device_region.guest_to_host_signal_table.interrupt_signalled_offset =
        device_region.guest_to_host_signal_table.offset_to_signal_table +
        (1 << device_region.guest_to_host_signal_table.num_nodes_lg2) *
            sizeof(int32_t);

    device_region.host_to_guest_signal_table.offset_to_signal_table =
        device_region.guest_to_host_signal_table.interrupt_signalled_offset +
        sizeof(device_region.guest_to_host_signal_table
                   .interrupt_signalled_offset);

    device_region.host_to_guest_signal_table.interrupt_signalled_offset =
        (1 << device_region.guest_to_host_signal_table.num_nodes_lg2) *
            sizeof(int32_t) +
        device_region.host_to_guest_signal_table.offset_to_signal_table;

    device_region.offset_of_region_data =
        device_region.host_to_guest_signal_table.interrupt_signalled_offset +
        sizeof(device_region.host_to_guest_signal_table
                   .interrupt_signalled_offset);

    *reinterpret_cast<vsoc_device_region *>(
        reinterpret_cast<char *>(mmap_addr) + offset) = device_region;
    offset += sizeof(vsoc_device_region);

    // Create one pair of eventfds for this region. Note that the guest to host
    // eventfd is non-blocking, whereas the host to guest eventfd is blocking.
    // This is in anticipation of blocking semantics for the host side locks.
    avd::SharedFD host_efd(avd::SharedFD::Event(0, EFD_NONBLOCK));
    LOG_IF(FATAL, !host_efd->IsOpen())
        << "Failed to create host eventfd for " << device_name << ": "
        << host_efd->StrError();

    avd::SharedFD guest_efd(avd::SharedFD::Event(0, EFD_NONBLOCK));
    LOG_IF(FATAL, !guest_efd->IsOpen())
        << "Failed to create guest eventfd for " << device_name << ": "
        << guest_efd->StrError();

    eventfd_data_.emplace(device_name, Region{host_efd, guest_efd});
  }

  munmap(mmap_addr, size_);
}

bool VSoCSharedMemoryImpl::GetEventFdPairForRegion(
    const std::string &region_name, avd::SharedFD *guest_to_host,
    avd::SharedFD *host_to_guest) const {
  auto it = eventfd_data_.find(region_name);
  if (it == eventfd_data_.end()) return false;

  *guest_to_host = it->second.host_fd;
  *host_to_guest = it->second.guest_fd;
  return true;
}

}  // anonymous namespace

std::unique_ptr<VSoCSharedMemory> VSoCSharedMemory::New(
    const uint32_t size_mb, const std::string &name, const Json::Value &root) {
  return std::unique_ptr<VSoCSharedMemory>(
      new VSoCSharedMemoryImpl(size_mb, name, root));
}

}  // namespace ivserver
