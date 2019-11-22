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
#include "host/commands/ivserver/ivserver.h"

#include <sys/select.h>
#include <algorithm>

#include <glog/logging.h>

#include "common/libs/fs/shared_select.h"
#include "host/commands/ivserver/hald_client.h"
#include "host/commands/ivserver/qemu_client.h"

namespace ivserver {

IVServer::IVServer(const IVServerOptions &options, int qemu_channel_fd,
                   int client_channel_fd)
    : vsoc_shmem_(VSoCSharedMemory::New(options.shm_file_path)) {
  if (qemu_channel_fd > 0) {
    qemu_channel_ = cvd::SharedFD::Dup(qemu_channel_fd);
  } else {
    LOG_IF(WARNING, unlink(options.qemu_socket_path.c_str()) == 0)
        << "Removed existing unix socket: " << options.qemu_socket_path
        << ". We can't confirm yet whether another instance is running.";
    qemu_channel_ = cvd::SharedFD::SocketLocalServer(
        options.qemu_socket_path.c_str(), false, SOCK_STREAM, 0666);
  }
  LOG_IF(FATAL, !qemu_channel_->IsOpen())
      << "Could not create QEmu channel: " << qemu_channel_->StrError();

  if (client_channel_fd > 0) {
    client_channel_ = cvd::SharedFD::Dup(client_channel_fd);
  } else {
    LOG_IF(WARNING, unlink(options.client_socket_path.c_str()) == 0)
        << "Removed existing unix socket: " << options.client_socket_path
        << ". We can't confirm yet whether another instance is running.";
    client_channel_ = cvd::SharedFD::SocketLocalServer(
        options.client_socket_path.c_str(), false, SOCK_STREAM, 0666);
  }
  LOG_IF(FATAL, !client_channel_->IsOpen())
      << "Could not create Client channel: " << client_channel_->StrError();
}

void IVServer::Serve() {
  while (true) {
    cvd::SharedFDSet rset;
    rset.Set(qemu_channel_);
    rset.Set(client_channel_);
    cvd::Select(&rset, nullptr, nullptr, nullptr);

    if (rset.IsSet(qemu_channel_)) {
      HandleNewQemuConnection();
    }

    if (rset.IsSet(client_channel_)) {
      HandleNewClientConnection();
    }
  }

  LOG(FATAL) << "Control reached out of event loop";
}

void IVServer::HandleNewClientConnection() {
  std::unique_ptr<HaldClient> res = HaldClient::New(
      *vsoc_shmem_, cvd::SharedFD::Accept(*client_channel_, nullptr, nullptr));
  if (!res) {
    LOG(WARNING) << "Rejecting unsuccessful HALD connection.";
  }
}

void IVServer::HandleNewQemuConnection() {
  std::unique_ptr<QemuClient> res = QemuClient::New(
      *vsoc_shmem_, cvd::SharedFD::Accept(*qemu_channel_, nullptr, nullptr));

  if (!res) {
    LOG(WARNING) << "Could not accept new QEmu client.";
  }
}

}  // namespace ivserver
