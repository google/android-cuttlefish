#include "vnc_server.h"

#include <string>
#include <algorithm>

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
  avd::vnc::VncServer vnc_server(kVncServerPort, HasAggressiveFlag(argc, argv));
  vnc_server.MainLoop();
}

