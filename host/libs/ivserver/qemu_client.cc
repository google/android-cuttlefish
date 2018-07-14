/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "host/libs/ivserver/qemu_client.h"

#include <glog/logging.h>

namespace ivserver {

std::unique_ptr<QemuClient> QemuClient::New(const VSoCSharedMemory& shmem,
                                            const cvd::SharedFD& socket) {
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

QemuClient::QemuClient(cvd::SharedFD socket) : client_socket_(socket) {}

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

  // 3. Connect notifications for existing other clients, if any.  This is
  //    a peer ID (number between 0 and 65535 other than the client's ID),
  //    repeated N times.  Each repetition is accompanied by one file
  //    descriptor.  These are for interrupting the peer with that ID using
  //    vector 0,..,N-1, in order.  If the client is configured for fewer
  //    vectors, it closes the extra file descriptors.  If it is configured
  //    for more, the extra vectors remain unconnected.
  for (const auto region_data : shmem.Regions()) {
    if (!SendSocketInfo(kHostID, region_data.host_fd)) {
      LOG(ERROR) << "Failed to send Host Side FD for region "
                 << region_data.device_name << ": " << client_socket_->StrError();
      return false;
    }
  }

  // 4. Interrupt setup.  This is the client's own ID, repeated N times.
  //    Each repetition is accompanied by one file descriptor.  These are
  //    for receiving interrupts from peers using vector 0,..,N-1, in
  //    order.  If the client is configured for fewer vectors, it closes
  //    the extra file descriptors.  If it is configured for more, the
  //    extra vectors remain unconnected.
  for (const auto region_data : shmem.Regions()) {
    if (!SendSocketInfo(kGuestID, region_data.guest_fd)) {
      LOG(ERROR) << "Failed to send Guest Side FD for region "
                 << region_data.device_name << ": " << client_socket_->StrError();
      return false;
    }
  }

  // 5. The number -1, accompanied by the file descriptor for the shared
  //    memory.
  if (!SendSocketInfo(kSharedMem, shmem.SharedMemFD())) {
    LOG(ERROR) << "Failed to send Shared Memory socket: "
               << client_socket_->StrError();
    return false;
  }


  LOG(INFO) << "QEmu handshake completed.";
  return true;
}

bool QemuClient::SendSocketInfo(QemuConstants message,
                                const cvd::SharedFD& socket) {
  struct iovec vec {
    &message, sizeof(message)
  };
  cvd::InbandMessageHeader hdr{nullptr, 0, &vec, 1, 0};
  cvd::SharedFD fds[] = {socket};
  int rval = client_socket_->SendMsgAndFDs(hdr, 0, fds);
  if (rval == -1) {
    LOG(ERROR) << "failed to send shared_mem_fd: "
               << client_socket_->StrError();
    return false;
  }
  return true;
}

}  // namespace ivserver
