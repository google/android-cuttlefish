#pragma once

#include <inttypes.h>
#include <json/json.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace ivserver {

const uint16_t kLayoutVersionMajor = 1;
const uint16_t kLayoutVersionMinor = 0;

//
// TODO(romitd): Make it a singleton.
//
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

  bool GetEventFDpairForRegion(const std::string &region_name,
                               int *guest_to_host, int *host_to_guest) const;

  inline int GetSharedMemoryFileDescriptor() const { return shmfd_; }
  inline int HasInitialized() const { return initialized_; }

 private:
  bool CreateLayout(void);

  const uint32_t size_;
  const Json::Value &json_root_;
  int shmfd_ = -1;
  bool initialized_ = false;
  std::vector<std::tuple<std::string, int, int>> eventfd_data_;

 public:
  const decltype(eventfd_data_) &GetEventFDData() const {
    return eventfd_data_;
  }
};

}  // namespace ivserver
