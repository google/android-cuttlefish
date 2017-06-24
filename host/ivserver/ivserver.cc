#include "host/ivserver/ivserver.h"
#include "host/ivserver/clienthandshake.h"
#include "host/ivserver/qemuhandshake.h"
#include "host/ivserver/socketutils.h"

#include <sys/select.h>
#include <algorithm>

#include <glog/logging.h>

#define LOG_TAG "ivserver::IVServer"

namespace ivserver {

IVServer::IVServer(const IVServerOptions &options, const Json::Value &json_root)
    : json_root_{json_root},
      vsoc_shmem_(options.shm_size_mib, options.shm_file_path, json_root_) {
  qemu_listener_fd_ = start_listener_socket(options.qemu_socket_path);
  if (qemu_listener_fd_ == -1) return;

  client_listener_fd_ = start_listener_socket(options.client_socket_path);
  if (client_listener_fd_ == -1) return;

  initialized_ = true;
}

void IVServer::Serve() {
  while (true) {
    fd_set readfdset;
    int retval;

    FD_ZERO(&readfdset);
    FD_SET(qemu_listener_fd_, &readfdset);
    FD_SET(client_listener_fd_, &readfdset);

    retval = select(std::max(qemu_listener_fd_, client_listener_fd_) + 1,
                    &readfdset, NULL, NULL, NULL);
    if (retval == -1) {
      LOG(ERROR) << "select failed";
      return;
    }

    if (FD_ISSET(qemu_listener_fd_, &readfdset)) {
      if (!HandleNewQemuConnection()) {
        LOG(ERROR) << "Unable to handle new QEMU connection";
        return;
      }
    } else if (FD_ISSET(client_listener_fd_, &readfdset)) {
      if (!HandleNewClientConnection()) {
        LOG(ERROR) << "Unable to handle new client connection";
        return;
      }
    } else {
      LOG(WARNING) << "select returned with invalid file-descriptor set";
    }
  }

  LOG(FATAL) << "Control reached out of event loop";
  return;  // This should never happen;
}

bool IVServer::HandleNewClientConnection() {
  ClientHandshake client_handshake(vsoc_shmem_, client_listener_fd_);
  if (!client_handshake.HasInitialized()) {
    return false;
  }

  return client_handshake.PerformHandshake();
}

bool IVServer::HandleNewQemuConnection() {
  QemuHandshake qemu_handshake(vsoc_shmem_, qemu_listener_fd_);
  if (!qemu_handshake.HasInitialized()) {
    return false;
  }

  return qemu_handshake.PerformHandshake();
}

}  // namespace ivserver
