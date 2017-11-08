#include "vnc_server.h"

#include <signal.h>
#include <algorithm>
#include <string>

namespace {
constexpr int kVncServerPort = 6444;

// TODO(haining) use gflags when available
bool HasAggressiveFlag(int argc, char* argv[]) {
  const std::string kAggressive = "--aggressive";
  auto end = argv + argc;
  return std::find(argv, end, kAggressive) != end;
}
}  // namespace

int main(int argc, char* argv[]) {
  struct sigaction new_action, old_action;
  memset(&new_action, 0, sizeof(new_action));
  new_action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &new_action, &old_action);
  avd::vnc::VncServer vnc_server(kVncServerPort, HasAggressiveFlag(argc, argv));
  vnc_server.MainLoop();
}
