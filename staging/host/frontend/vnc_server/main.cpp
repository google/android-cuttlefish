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

#include <algorithm>
#include <string>

#include <gflags/gflags.h>

#include "android-base/logging.h"
#include "host/frontend/vnc_server/vnc_server.h"
#include "host/frontend/vnc_server/vnc_utils.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_bool(agressive, false, "Whether to use agressive server");
DEFINE_int32(port, 6444, "Port where to listen for connections");

int main(int argc, char* argv[]) {
  using ::android::base::ERROR;
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  cvd::vnc::VncServer vnc_server(FLAGS_port, FLAGS_agressive);
  vnc_server.MainLoop();
}
