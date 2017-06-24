#include "host/ivserver/clienthandshake.h"
#include "host/ivserver/socketutils.h"

#include <string>

#include <glog/logging.h>

namespace ivserver {

#define LOG_TAG "ivserver::ClientHandshake"

ClientHandshake::ClientHandshake(const VSoCSharedMemory &shared_mem,
                                 const int client_listener_socket)
    : shared_mem_{shared_mem} {
  client_socket_ = handle_new_connection(client_listener_socket);
  if (client_socket_ == -1) {
    LOG(ERROR) << "couldn't get a new socket for Client Connection.";
    return;
  }

  initialized_ = true;
}

/*
 * TODO(romitd): Refactor.
 * TODO(romitd): move this to a separate thread.
 */
bool ClientHandshake::PerformHandshake() {
  bool rval;

  rval = send_msg(client_socket_, kHostClientProtocolVersion);
  if (!rval) {
    LOG(ERROR) << "failed to send protocol_version.";
    return false;
  }

  int16_t region_name_len = recv_msg_int16(client_socket_);
  if (region_name_len == -1) {
    LOG(ERROR) << "error receiving region name_length.";
    return false;
  }

  std::shared_ptr<std::string> region_name =
      recv_msg(client_socket_, static_cast<int>(region_name_len));
  if (!region_name) {
    LOG(ERROR) << "error receiving region name.";
    return false;
  }

  int guest_to_host_efd = -1;
  int host_to_guest_efd = -1;

  rval = shared_mem_.GetEventFDpairForRegion(*region_name, &guest_to_host_efd,
                                             &host_to_guest_efd);
  // Region not found.
  if (!rval) {
    rval = send_msg(client_socket_, static_cast<int32_t>(-1));
    if (!rval) {
      LOG(ERROR) << "error in sending region_not_found";
    }
    return false;
  }

  // Send the eventfds
  rval = send_msg(client_socket_, guest_to_host_efd, 0);
  if (!rval) {
    LOG(ERROR) << "error in sending guest_to_host_eventfd";
    return false;
  }

  rval = send_msg(client_socket_, host_to_guest_efd, 0);
  if (!rval) {
    LOG(ERROR) << "error in sending host_to_guest_eventfd";
    return false;
  }

  // Send the shmfd
  rval =
      send_msg(client_socket_, shared_mem_.GetSharedMemoryFileDescriptor(), 0);
  if (!rval) {
    LOG(ERROR) << "error in sending shared_mem_fd";
    return false;
  }

  return true;
}

}  // namespace ivserver
