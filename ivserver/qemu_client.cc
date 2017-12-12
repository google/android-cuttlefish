#include "host/ivserver/qemu_client.h"

#include <glog/logging.h>

namespace ivserver {

std::unique_ptr<QemuClient> QemuClient::New(const VSoCSharedMemory& shmem,
                                            const avd::SharedFD& socket) {
  std::unique_ptr<QemuClient> res;
  if (!socket->IsOpen()) {
    LOG(WARNING) << "Invalid socket passed to QemuClient: "
                 << socket->StrError();
    return res;
  }

  res.reset(new QemuClient(std::move(socket)));
  if (!res->PerformHandshake(shmem)) {
    LOG(ERROR) << "Qemu handshake failed. Dropping connection.";
    res.reset();
  }

  return res;
}

QemuClient::QemuClient(avd::SharedFD socket) : client_socket_(socket) {}

// Once the QemuClient object is constructed, invoking the following
// method will perform the actual handshake with a QEMU instance.
bool QemuClient::PerformHandshake(const VSoCSharedMemory& shmem) {
  LOG(INFO) << "New QEmu client connected.";
  // 1. The protocol version number, currently zero.  The client should
  //    close the connection on receipt of versions it can't handle.
  int64_t msg = QemuConstants::kIvshMemProtocolVersion;
  int rval = client_socket_->Send(&msg, sizeof(msg), MSG_NOSIGNAL);
  if (rval != sizeof(msg)) {
    LOG(ERROR) << "Failed to send protocol version: "
               << client_socket_->StrError();
    return false;
  }

  // 2. The client's ID.  This is unique among all clients of this server.
  //    IDs must be between 0 and 65535, because the Doorbell register
  //    provides only 16 bits for them.
  msg = QemuConstants::kGuestID;
  rval = client_socket_->Send(&msg, sizeof(msg), MSG_NOSIGNAL);
  if (rval != sizeof(msg)) {
    LOG(ERROR) << "Failed to send VM Id: " << client_socket_->StrError();
    return false;
  }

  // 3. The number -1, accompanied by the file descriptor for the shared
  //    memory.
  if (!SendSocketInfo(QemuFDMsg::kSharedMem, shmem.SharedMemFD())) {
    LOG(ERROR) << "Failed to send Shared Memory socket: "
               << client_socket_->StrError();
    return false;
  }

  // 4. Connect notifications for existing other clients, if any.  This is
  //    a peer ID (number between 0 and 65535 other than the client's ID),
  //    repeated N times.  Each repetition is accompanied by one file
  //    descriptor.  These are for interrupting the peer with that ID using
  //    vector 0,..,N-1, in order.  If the client is configured for fewer
  //    vectors, it closes the extra file descriptors.  If it is configured
  //    for more, the extra vectors remain unconnected.
  for (const auto region_pair : shmem.Regions()) {
    if (!SendSocketInfo(QemuFDMsg::kHostSideHald, region_pair.second.host_fd)) {
      LOG(ERROR) << "Failed to send Host Side FD for region "
                 << region_pair.first << ": " << client_socket_->StrError();
      return false;
    }
  }

  // 5. Interrupt setup.  This is the client's own ID, repeated N times.
  //    Each repetition is accompanied by one file descriptor.  These are
  //    for receiving interrupts from peers using vector 0,..,N-1, in
  //    order.  If the client is configured for fewer vectors, it closes
  //    the extra file descriptors.  If it is configured for more, the
  //    extra vectors remain unconnected.
  for (const auto region_pair : shmem.Regions()) {
    if (!SendSocketInfo(QemuFDMsg::kGuestSideHal,
                        region_pair.second.guest_fd)) {
      LOG(ERROR) << "Failed to send Guest Side FD for region "
                 << region_pair.first << ": " << client_socket_->StrError();
      return false;
    }
  }

  LOG(INFO) << "QEmu handshake completed.";
  return true;
}

bool QemuClient::SendSocketInfo(QemuFDMsg message,
                                const avd::SharedFD& socket) {
  struct iovec vec {
    &message, sizeof(message)
  };
  avd::InbandMessageHeader hdr{nullptr, 0, &vec, 1, 0};
  avd::SharedFD fds[] = {socket};
  int rval = client_socket_->SendMsgAndFDs(hdr, 0, fds);
  if (rval == -1) {
    LOG(ERROR) << "failed to send shared_mem_fd: "
               << client_socket_->StrError();
    return false;
  }
  return true;
}

}  // namespace ivserver