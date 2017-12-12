#include "vnc_server.h"

#include <algorithm>
#include <string>

DEFINE_bool(agressive, false, "Whether to use agressive server");
DEFINE_int32(port, 6444, "Port where to listen for connections");

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  google::ParseCommandLineFlags(&argc, &argv, true);
  avd::vnc::VncServer vnc_server(FLAGS_port, FLAGS_agressive);
  vnc_server.MainLoop();
}
