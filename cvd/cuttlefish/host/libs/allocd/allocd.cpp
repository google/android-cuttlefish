/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <android-base/logging.h>
#include <asm-generic/socket.h>
#include <gflags/gflags.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

#include "common/libs/fs/shared_fd.h"
#include "host/libs/allocd/alloc_utils.h"
#include "host/libs/allocd/request.h"
#include "host/libs/allocd/resource_manager.h"
#include "host/libs/config/logging.h"

DEFINE_string(socket_path, cuttlefish::kDefaultLocation, "Socket path");
DEFINE_bool(ebtables_legacy, false, "use ebtables-legacy instead of ebtables");

int main(int argc, char* argv[]) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);

  google::ParseCommandLineFlags(&argc, &argv, true);

  cuttlefish::SharedFD FinalFD;
  {
    cuttlefish::ResourceManager m;
    m.SetSocketLocation(FLAGS_socket_path);
    m.SetUseEbtablesLegacy(FLAGS_ebtables_legacy);
    m.JsonServer();
  }

  return 0;  // EXIT_SUCCESS? or status
}
