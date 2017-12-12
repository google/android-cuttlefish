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

#include <getopt.h>
#include <algorithm>
#include <string>

#include "host/frontend/vnc_server/vnc_server.h"

bool FLAGS_agressive = false;
bool FLAGS_debug = false;
std::string FLAGS_input_socket("/tmp/android-cuttlefish-1-input");

bool FLAGS_port = 6444;

static struct option opts[] = {
  {"agressive", no_argument, NULL, 'a'},
  {"debug", no_argument, NULL, 'd'},
  {"input_socket", required_argument, NULL, 'i'},
  {"port", required_argument, NULL, 'p'},
  {0, 0, 0, 0}
};

int main(int argc, char* argv[]) {
  using ::android::base::ERROR;
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  long port = -1;
  char *endp = NULL;
  bool error = false;
  char c;
  while ((c = getopt_long(argc, argv, "", opts, nullptr)) != -1) {
    switch(c) {
      case 'a':
        FLAGS_agressive = true;
        break;
      case 'd':
        FLAGS_debug = true;
        break;
      case 'i':
        FLAGS_input_socket = optarg;
        break;
      case 'p':
        port = strtol(optarg, &endp, 10);
        if (*endp || (port <= 0) || (port > 65536)) {
          LOG(ERROR) << "Port must be an integer > 0 and < 65536";
          error = true;
        }
        FLAGS_port = port;
        break;
    }
  }
  if (error) {
    exit(2);
  }
  avd::vnc::VncServer vnc_server(FLAGS_port, FLAGS_agressive);
  vnc_server.MainLoop();
}
