/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace {
void RunCommand(const char* command) {
  int rval = std::system(command);
  if (rval) {
    LOG(ERROR) << "Unable to execute command: " << command
               << ". Exit code: " << rval;
  }
}
}  // anonymous namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  int exit_code = 0;

  // TODO(b/78512938): Should ask the monitor to do the shutdown instead
  auto vm_manager = vm_manager::VmManager::Get();
  if (!vm_manager->Stop()) {
    LOG(ERROR)
        << "Error when stopping guest virtual machine. Is it still running?";
    exit_code = 1;
  }

  auto config = vsoc::CuttlefishConfig::Get();

  std::string run_files = config->PerInstancePath("*");
  std::string fuser_cmd = "fuser -k ";
  fuser_cmd += run_files;
  fuser_cmd += " ";
  fuser_cmd += config->mempath();
  RunCommand(fuser_cmd.c_str());
}
