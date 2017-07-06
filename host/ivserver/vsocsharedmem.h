#pragma once

#include <inttypes.h>
#include <json/json.h>
#include <unistd.h>
#include <map>
#include <memory>
#include <string>

#include "common/libs/fs/shared_fd.h"

namespace ivserver {

class VSoCSharedMemory {
 public:
  // Max name length of a memory region.
  // TODO(ender): set this value from trusted source.
  static constexpr int32_t kMaxRegionNameLength = 16;

  VSoCSharedMemory() = default;
  virtual ~VSoCSharedMemory() = default;

  static std::unique_ptr<VSoCSharedMemory> New(const uint32_t size_mib,
                                               const std::string &name,
                                               const Json::Value &json_root);

  virtual bool GetEventFdPairForRegion(const std::string &region_name,
                               avd::SharedFD *guest_to_host,
                               avd::SharedFD *host_to_guest) const = 0;

  virtual const avd::SharedFD &shared_mem_fd() const = 0;

  virtual void BroadcastQemuSocket(const avd::SharedFD &qemu_socket) const = 0;

 private:
  VSoCSharedMemory(const VSoCSharedMemory &) = delete;
  VSoCSharedMemory& operator=(const VSoCSharedMemory& other) = delete;
};

}  // namespace ivserver
