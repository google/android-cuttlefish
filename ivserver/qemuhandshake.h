
#include "host/ivserver/vsocsharedmem.h"

#include <inttypes.h>
#include <unistd.h>
#include <memory>

namespace ivserver {

// TODO(romitd): Refactor ClientHandshake and QemuHandshake into a base
// class. Have derived classes with handshake specific logic.

// Handles an ivserver to QEMU connection & handshake.
// This is loosely based on the spec found under
// $(QEMU_SRC)/docs/specs/ivshmem-spec.txt
// where QEMU_SRC is assumed to point to the path of QEMU source code.
// Alternatively, please point your browser to the following URL:
// https://github.com/qemu/qemu/blob/master/docs/specs/ivshmem-spec.txt
class QemuHandshake final {
 public:
  QemuHandshake(const QemuHandshake &) = delete;
  QemuHandshake(const VSoCSharedMemory &shared_mem,
                const int qemu_listener_socket);

  ~QemuHandshake() {
    if (qemu_socket_ != -1) {
      close(qemu_socket_);
    }
  }

  //
  // Once the QemuHandshake object is constructed, invoking the following
  // method will perform the actual handshake with a QEMU instance.
  //
  bool PerformHandshake(void);

 private:
  const VSoCSharedMemory &shared_mem_;
  int qemu_socket_ = -1;
};

}  // namespace ivserver
