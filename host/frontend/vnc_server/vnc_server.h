#ifndef DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_VNC_SERVER_H_
#define DEVICE_GOOGLE_GCE_GCE_UTILS_GCE_VNC_SERVER_VNC_SERVER_H_

#include "blackboard.h"
#include "frame_buffer_watcher.h"
#include "jpeg_compressor.h"
#include "tcp_socket.h"
#include "virtual_inputs.h"
#include "vnc_client_connection.h"
#include "vnc_utils.h"

#include <string>
#include <thread>
#include <utility>

namespace avd {
namespace vnc {

class VncServer {
 public:
  explicit VncServer(int port, bool aggressive);

  VncServer(const VncServer&) = delete;
  VncServer& operator=(const VncServer&) = delete;

  [[noreturn]] void MainLoop();

 private:
  void StartClient(ClientSocket sock);

  void StartClientThread(ClientSocket sock);

  ServerSocket server_;
  VirtualInputs virtual_inputs_;
  BlackBoard bb_;
  FrameBufferWatcher frame_buffer_watcher_;
  bool aggressive_{};
};

}  // namespace vnc
}  // namespace avd

#endif
