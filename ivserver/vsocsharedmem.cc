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

static_assert(CURRENT_VSOC_LAYOUT_MAJOR_VERSION == 1,
              "Region layout code must be updated");

class VSoCSharedMemoryImpl : public VSoCSharedMemory {
 public:
  // Keeping the structure public for testability of RegionAllocation
  struct RegionOffset {
    uint32_t start_offset;
    uint32_t end_offset;
  };

  // Marked as static for testability of RegionAllocation
  static std::unique_ptr<std::vector<RegionOffset>> RegionAllocation(
      const uint32_t shm_size, const std::vector<uint32_t> &region_size);

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
  class RegionAllocator {
   public:
    explicit RegionAllocator(uint32_t max_size, uint32_t offset = 0)
        : max_size_{max_size}, offset_{offset} {}
    uint32_t Allocate(uint32_t size);
    uint32_t AllocateRest();

   private:
    uint32_t max_size_;
    uint32_t offset_;
  };
};

uint32_t VSoCSharedMemoryImpl::RegionAllocator::Allocate(uint32_t size) {
  if ((size + offset_) > max_size_) {
    LOG(FATAL) << "offset allocation will overflow memory region";
  }

  offset_ += size;
  return (offset_ - size);
}

uint32_t VSoCSharedMemoryImpl::RegionAllocator::AllocateRest() {
  return Allocate(max_size_ - offset_);
}

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

std::unique_ptr<std::vector<VSoCSharedMemoryImpl::RegionOffset>>
VSoCSharedMemoryImpl::RegionAllocation(
    const uint32_t shm_size, const std::vector<uint32_t> &region_size) {
  auto region_offset =
      std::unique_ptr<std::vector<RegionOffset>>{new std::vector<RegionOffset>};

  LOG_IF(FATAL, !region_offset) << "Error in memory allocation.";

  const long pagesize = sysconf(_SC_PAGESIZE);

  // Region size should be non-zero & a multiple of pagesize.
  std::all_of(region_size.begin(), region_size.end(), [&pagesize](size_t size) {
    if (size && ((size & (pagesize - 1)) == 0)) {
      return true;
    } else if (size == 0) {
      LOG(FATAL) << "region size is 0";
    } else {
      LOG(FATAL) << "region size " << size << " is not a multiple of pagesize "
                 << pagesize;
    }
  });

  // First page is reserved for region descriptors.
  // TODO(romitd): Support allocation when descriptors don't fit in page 0.
  //  In other words when there are many region descriptors and they don't
  //  fit in page 0.
  const uint32_t total_region_size =
      std::accumulate(region_size.begin(), region_size.end(), 0);
  if (total_region_size > (shm_size - pagesize)) {
    LOG(FATAL) << "Shared memory size " << shm_size
               << " is smaller than total memory requested "
               << total_region_size;
  }

  uint32_t current_offset = pagesize;

  for (const auto &size : region_size) {
    RegionOffset offset{current_offset, current_offset + size};
    current_offset += size;
    (*region_offset).push_back(offset);
  }

  return region_offset;
}

void VSoCSharedMemoryImpl::CreateLayout() {
  uint32_t offset = 0;
  void *mmap_addr =
      shared_mem_fd_->Mmap(0, size_, PROT_READ | PROT_WRITE, MAP_SHARED, 0);
  LOG_IF(FATAL, mmap_addr == MAP_FAILED)
      << "Error mmaping file: " << strerror(errno);

  vsoc_shm_layout_descriptor layout_descriptor{};

  layout_descriptor.major_version = CURRENT_VSOC_LAYOUT_MAJOR_VERSION;
  layout_descriptor.minor_version = CURRENT_VSOC_LAYOUT_MINOR_VERSION;
  layout_descriptor.size = size_;

  // TODO(romitd): error checking and sanity.
  // TODO(romitd): Refactor
  layout_descriptor.region_count = json_root_["vsoc_device_regions"].size();

  layout_descriptor.vsoc_region_desc_offset =
      json_root_["vsoc_shm_layout_descriptor"]["vsoc_region_desc_offset"]
          .asUInt();

  *reinterpret_cast<vsoc_shm_layout_descriptor *>(
      reinterpret_cast<char *>(mmap_addr) + offset) = layout_descriptor;

  // Gather the region sizes for allocating the start and end offsets.
  std::vector<uint32_t> region_size;
  for (const auto &region : json_root_["vsoc_device_regions"]) {
    region_size.push_back(region["region_size"].asUInt());
  }

  auto region_offsets = RegionAllocation(size_, region_size);

  // Move to the region_descriptor area.
  offset += layout_descriptor.vsoc_region_desc_offset;

  uint16_t region_idx = 0;

  for (const auto &region : json_root_["vsoc_device_regions"]) {
    vsoc_device_region device_region{};

    device_region.current_version = region["current_version"].asUInt();
    device_region.min_compatible_version =
        region["min_compatible_version"].asUInt();

    device_region.region_begin_offset =
        (*region_offsets)[region_idx].start_offset;
    device_region.region_end_offset = (*region_offsets)[region_idx].end_offset;
    ++region_idx;

    device_region.guest_to_host_signal_table.num_nodes_lg2 =
        region["guest_to_host_signal_table"]["num_nodes_lg2"].asUInt();

    device_region.host_to_guest_signal_table.num_nodes_lg2 =
        region["host_to_guest_signal_table"]["num_nodes_lg2"].asUInt();

    const std::string &device_name = region["device_name"].asString();
    strncpy(device_region.device_name, device_name.c_str(),
            sizeof(device_region.device_name) - 1);

    RegionAllocator allocator(region["region_size"].asUInt());

    // guest to host signal table starts at the beginning of the region.
    // Note that the offset could be different in future versions.
    device_region.guest_to_host_signal_table.futex_uaddr_table_offset =
        allocator.Allocate(
            (1 << device_region.guest_to_host_signal_table.num_nodes_lg2) *
            sizeof(uint32_t));
    device_region.guest_to_host_signal_table.interrupt_signalled_offset =
        allocator.Allocate(sizeof(uint32_t));

    // host to guest signal table starts immediately after guest to host signal
    // table & its interrupt signal area.
    device_region.host_to_guest_signal_table.futex_uaddr_table_offset =
        allocator.Allocate(
            (1 << device_region.guest_to_host_signal_table.num_nodes_lg2) *
            sizeof(uint32_t));
    device_region.host_to_guest_signal_table.interrupt_signalled_offset =
        allocator.Allocate(sizeof(uint32_t));

    // The offset of region_data starts immediately after host to guest
    // signal table & its interrupt signal area.
    device_region.offset_of_region_data = allocator.AllocateRest();

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
