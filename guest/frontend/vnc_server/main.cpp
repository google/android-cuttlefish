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
  cvd::vnc::VncServer vnc_server(kVncServerPort, HasAggressiveFlag(argc, argv));
  vnc_server.MainLoop();
}
