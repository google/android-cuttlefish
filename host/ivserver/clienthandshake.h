#pragma once

#include "host/ivserver/vsocsharedmem.h"

#include <inttypes.h>
#include <unistd.h>
#include <memory>

namespace ivserver {

// TODO(romitd): Refactor ClientHandshake and QemuHandshake into a base
// class. Have derived classes with handshake specific logic.

// Handles a host-client connection & handshake.
// This class encapsulates the host-client to server communication over UNIX
// domain socket. i.e. client sends a string identifying the name of the
// region it is interested in. The server would send status and 3 fds
// First the shm fd, second the guest_to_host eventfd and third the
// host_to_guest eventfd.
// If the region is not found approprate status (-1) is sent.
// Note that for every new client connected a unique ClientConnection object
// will be created and after the handshake it will be destroyed.
//
class ClientHandshake final {
 public:
  ClientHandshake(const ClientHandshake &) = delete;
  ClientHandshake(const VSoCSharedMemory &shared_mem,
                  const int client_listener_socket);

  ~ClientHandshake() {
    if (client_socket_ != -1) {
      close(client_socket_);
    }
  }

  //
  // Once the ClientHandshake object is constructed, invoking the following
  // method will perform the actual handshake with the client.
  //
  bool PerformHandshake();

  //
  // Will return true if the object has been successfully initialized.
  //
  inline bool HasInitialized() const { return initialized_; }

 private:
  const VSoCSharedMemory &shared_mem_;
  int client_socket_ = -1;
  bool initialized_ = false;
};

}  // namespace ivserver
