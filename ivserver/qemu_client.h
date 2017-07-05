#pragma once

#include <memory>

#include "common/libs/fs/shared_fd.h"
#include "host/ivserver/vsocsharedmem.h"

namespace ivserver {

// QemuClient manages individual QEmu connections using protocol specified
// in documentation file distributed as part of QEmu 2.8 package under:
// docs/specs/ivshmem-spec.txt
// Alternatively, please point your browser to the following URL:
// https://github.com/qemu/qemu/blob/stable-2.8/docs/specs/ivshmem-spec.txt
class QemuClient final {
 public:
  static std::unique_ptr<QemuClient> New(const avd::SharedFD &connection,
                                         avd::SharedFD shmemfd);

  avd::SharedFD client_socket() const { return client_socket_; }

 private:
  avd::SharedFD client_socket_;

  // Initialize new instance of QemuClient.
  QemuClient(avd::SharedFD qemu_listener_socket);

  // Once the QemuClient object is constructed, invoking the following
  // method will perform the actual handshake with a QEMU instance.
  bool PerformHandshake(const avd::SharedFD &shmem_fd);

  QemuClient(const QemuClient &) = delete;
  QemuClient &operator=(const QemuClient &) = delete;
};

}  // namespace ivserver
