#pragma once
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

#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/ivserver/vsocsharedmem.h"

namespace ivserver {

// QemuClient manages individual QEmu connections using protocol specified
// in documentation file distributed as part of QEmu 2.8 package under:
// docs/specs/ivshmem-spec.txt
// Alternatively, please point your browser to the following URL:
// https://github.com/qemu/qemu/blob/stable-2.8/docs/specs/ivshmem-spec.txt
class QemuClient final {
 public:
  static std::unique_ptr<QemuClient> New(const VSoCSharedMemory &shmem,
                                         const avd::SharedFD &connection);

  avd::SharedFD client_socket() const { return client_socket_; }

 private:
  enum QemuConstants : int64_t {
    kIvshMemProtocolVersion = 0,
    // HostID is in fact a Peer ID and can take multiple values, depending on
    // how many subsystems we would like Guest to talk to.
    kHostBaseID = 0,
    // GuestID is a unique form of Peer ID (see above), that identifies newly
    // created quest in IvSharedMem world.
    kGuestID = 1024
  };

  static_assert(QemuConstants::kHostBaseID < QemuConstants::kGuestID,
                "Guest and host should have different IDs");
  // Type of QEmu FD messages.
  // QEmu uses these messages to identify purpose of socket it is
  // receiving.
  enum class QemuFDMsg : int64_t {
    // Represents SharedMemory FD.
    kSharedMem = -1,
    // Represents primary (and currently only) FD that is owned and managed by
    // Host side.
    kHostSideHald = QemuConstants::kHostBaseID,
    // Represents FDs that are owned by Guest.
    kGuestSideHal = QemuConstants::kGuestID,
  };

  avd::SharedFD client_socket_;

  // Initialize new instance of QemuClient.
  QemuClient(avd::SharedFD qemu_listener_socket);

  // Once the QemuClient object is constructed, invoking the following
  // method will perform the actual handshake with a QEMU instance.
  bool PerformHandshake(const VSoCSharedMemory &shmem_fd);

  // Send socket data to Qemu.
  bool SendSocketInfo(QemuFDMsg message, const avd::SharedFD &socket);

  QemuClient(const QemuClient &) = delete;
  QemuClient &operator=(const QemuClient &) = delete;
};

}  // namespace ivserver
