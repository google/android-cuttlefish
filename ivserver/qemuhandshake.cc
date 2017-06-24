#include "host/ivserver/qemuhandshake.h"
#include "host/ivserver/socketutils.h"

#include <glog/logging.h>
#include <tuple>

#define LOG_TAG "ivserver::QemuHandshake"

namespace ivserver {

//
// TODO(romitd): We might need to disallow more than one handshakes.
//
QemuHandshake::QemuHandshake(const VSoCSharedMemory &shared_mem,
                             const int qemu_listener_socket)
    : shared_mem_{shared_mem} {
  qemu_socket_ = handle_new_connection(qemu_listener_socket);
  if (qemu_socket_ == -1) {
    LOG(FATAL) << "couldn't get a new socket for QEMU Connection.";
    return;
  }

  has_initialized_ = true;
}

/*
 * TODO(romitd): Refactor.
 * TODO(romitd): Move this to a separate thread.
 */
bool QemuHandshake::PerformHandshake(void) {
  int rval;
  rval = send_msg(qemu_socket_, kQemuIvshMemProtocolVersion);
  if (rval == -1) {
    LOG(ERROR) << "Failed to send protocol_version.";
    return false;
  }

  rval = send_msg(qemu_socket_, kQemuVMId);
  if (rval == -1) {
    LOG(ERROR) << "Failed to send VM Id.";
    return false;
  }

  rval =
      send_msg(qemu_socket_, shared_mem_.GetSharedMemoryFileDescriptor(), -1);
  if (rval == -1) {
    LOG(ERROR) << "failed to send shared_mem_fd.";
    return false;
  }

  for (const auto &eventfd_data : shared_mem_.GetEventFDData()) {
    int g_to_h_efd = std::get<1>(eventfd_data);
    rval = send_msg(qemu_socket_, g_to_h_efd, 0);
    if (rval == -1) {
      LOG(ERROR) << "failed to send a guest to host eventfd.";
      return false;
    }
  }

  for (const auto &eventfd_data : shared_mem_.GetEventFDData()) {
    int h_to_g_efd = std::get<2>(eventfd_data);
    rval = send_msg(qemu_socket_, h_to_g_efd, 1);
    if (rval == -1) {
      LOG(ERROR) << "failed to send a host to guest eventfd.";
      return false;
    }
  }

  return true;
}

}  // namespace ivserver
