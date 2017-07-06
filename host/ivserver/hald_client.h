#pragma once

#include <inttypes.h>
#include <unistd.h>
#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/ivserver/vsocsharedmem.h"

namespace ivserver {

// Handles a HAL deamon client connection & handshake.
class HaldClient final {
 public:
  static std::unique_ptr<HaldClient> New(const VSoCSharedMemory &shared_mem,
                                         const avd::SharedFD &client_fd);

 private:
  avd::SharedFD client_socket_;

  // Initialize new instance of HAL Client.
  HaldClient(const avd::SharedFD &client_fd);

  // Perform handshake with HAL Client.
  // If the region is not found approprate status (-1) is sent.
  // Note that for every new client connected a unique ClientConnection object
  // will be created and after the handshake it will be destroyed.
  bool PerformHandshake(const VSoCSharedMemory &shared_mem);

  HaldClient(const HaldClient &) = delete;
  HaldClient &operator=(const HaldClient &) = delete;
};

}  // namespace ivserver
