#pragma once

#include <json/json.h>
#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/ivserver/options.h"
#include "host/ivserver/vsocsharedmem.h"

namespace ivserver {

// This class is responsible for orchestrating the setup and then serving
// new connections.
class IVServer final {
 public:
  IVServer(const IVServerOptions &options, const Json::Value &json_root);
  IVServer(const IVServer &) = delete;

  // Serves incoming client and qemu connection.
  // This method should never return.
  void Serve();

 private:
  void HandleNewClientConnection();
  void HandleNewQemuConnection();

  const Json::Value &json_root_;
  std::unique_ptr<VSoCSharedMemory> vsoc_shmem_;
  avd::SharedFD qemu_channel_;
  avd::SharedFD client_channel_;
};

}  // namespace ivserver
