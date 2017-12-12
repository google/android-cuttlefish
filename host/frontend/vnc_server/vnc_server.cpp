#include "vnc_server.h"
#include "blackboard.h"
#include "frame_buffer_watcher.h"
#include "jpeg_compressor.h"
#include "tcp_socket.h"
#include "virtual_inputs.h"
#include "vnc_client_connection.h"
#include "vnc_utils.h"

using avd::vnc::VncServer;

VncServer::VncServer(int port, bool aggressive)
    : server_(port), frame_buffer_watcher_{&bb_}, aggressive_{aggressive} {}

void VncServer::MainLoop() {
  while (true) {
    auto connection = server_.Accept();
    StartClient(std::move(connection));
  }
}

void VncServer::StartClient(ClientSocket sock) {
  std::thread t(&VncServer::StartClientThread, this, std::move(sock));
  t.detach();
}

void VncServer::StartClientThread(ClientSocket sock) {
  // NOTE if VncServer is expected to be destroyed, we have a problem here.
  // All of the client threads will be pointing to the VncServer's
  // data members. In the current setup, if the VncServer is destroyed with
  // clients still running, the clients will all be left with dangling
  // pointers.
  VncClientConnection client(std::move(sock), &virtual_inputs_, &bb_,
                             aggressive_);
  client.StartSession();
}
