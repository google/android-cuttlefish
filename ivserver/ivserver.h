#pragma once

#include "host/ivserver/options.h"
#include "host/ivserver/vsocsharedmem.h"

#include <json/json.h>
#include <memory>

namespace ivserver {

//
// This class is responsible for orchestrating the setup and then serving
// new connections.
//
class IVServer final {
 public:
  IVServer(const IVServerOptions &options, const Json::Value &json_root);
  IVServer(const IVServer &) = delete;

  //
  // Serves incoming client and qemu connection.
  // This method should never return.
  //
  void Serve();

  //
  // Returns true if object has been successfully initialized.
  //
  bool HasInitialized() const { return initialized_; }

 private:
  bool HandleNewClientConnection();
  bool HandleNewQemuConnection();

  const Json::Value &json_root_;
  VSoCSharedMemory vsoc_shmem_;
  int qemu_listener_fd_ = -1;
  int client_listener_fd_ = -1;
  bool initialized_ = false;
};

}  // namespace ivserver
