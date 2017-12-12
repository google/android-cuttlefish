#include "host/ivserver/qemu_client.h"

#include <glog/logging.h>

namespace ivserver {
namespace {
// QEMU expects version 0 of the QEMU <--> ivserver protocol.
const uint64_t kQemuIvshMemProtocolVersion = 0;
const uint64_t kQemuVMId = 1;
}  // anonymous namespace

std::unique_ptr<QemuClient> QemuClient::New(const avd::SharedFD& socket,
                                            avd::SharedFD shmemfd) {
  std::unique_ptr<QemuClient> res;
  if (!socket->IsOpen()) {
    LOG(WARNING) << "Invalid socket passed to QemuClient: "
                 << socket->StrError();
    return res;
  }

  res.reset(new QemuClient(std::move(socket)));
  if (!res->PerformHandshake(shmemfd)) {
    LOG(ERROR) << "QEmu handshake failed. Dropping connection.";
    res.reset();
  }

  return res;
}

QemuClient::QemuClient(avd::SharedFD socket) : client_socket_(socket) {}

// Once the QemuClient object is constructed, invoking the following
// method will perform the actual handshake with a QEMU instance.
bool QemuClient::PerformHandshake(const avd::SharedFD& shmem_fd) {
  int rval =
      client_socket_->Send(&kQemuIvshMemProtocolVersion,
                           sizeof(kQemuIvshMemProtocolVersion), MSG_NOSIGNAL);
  if (rval != sizeof(kQemuIvshMemProtocolVersion)) {
    LOG(ERROR) << "Failed to send protocol version: "
               << client_socket_->StrError();
    return false;
  }

  rval = client_socket_->Send(&kQemuVMId, sizeof(kQemuVMId), MSG_NOSIGNAL);
  if (rval != sizeof(kQemuVMId)) {
    LOG(ERROR) << "Failed to send VM Id: " << client_socket_->StrError();
    return false;
  }

  // Send FD to remote process over unix domain socket using control message.
  uint64_t control_data = ~0;
  struct iovec vec {
    &control_data, sizeof(control_data)
  };
  avd::InbandMessageHeader hdr{nullptr, 0, &vec, 1, 0};
  avd::SharedFD fds[] = {shmem_fd};
  rval = client_socket_->SendMsgAndFDs(hdr, 0, fds);
  if (rval == -1) {
    LOG(ERROR) << "failed to send shared_mem_fd: "
               << client_socket_->StrError();
    return false;
  }

  return true;
}

}  // namespace ivserver
