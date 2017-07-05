#pragma once

#include <inttypes.h>
#include <json/json.h>
#include <unistd.h>
#include <map>
#include <memory>
#include <string>

namespace ivserver {

class VSoCSharedMemory final {
 public:
  VSoCSharedMemory(const uint32_t &size_mib, const std::string &name,
                   const Json::Value &json_root);
  VSoCSharedMemory(const VSoCSharedMemory &) = delete;

  ~VSoCSharedMemory() {
    if (shmfd_ != -1) {
      close(shmfd_);
    }
  }

  bool GetEventFdPairForRegion(const std::string &region_name,
                               int *guest_to_host, int *host_to_guest) const;

  inline int GetSharedMemoryFileDescriptor() const { return shmfd_; }

  void BroadcastQemuSocket(int qemu_socket) const;

 private:
  void CreateLayout();

  const uint32_t size_;
  const Json::Value &json_root_;
  int shmfd_ = -1;
  std::map<std::string, std::pair<int, int>> eventfd_data_;
};

}  // namespace ivserver
