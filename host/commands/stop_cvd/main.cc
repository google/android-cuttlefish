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

#include "host/libs/config/host_config.h"

using vsoc::GetDefaultPerInstancePath;
using vsoc::GetPerInstanceDefault;

DECLARE_int32(instance);
// TODO(b/78512938): These parameters should go away when the launcher work is
// completed and the process monitor handles the shutdown.
DEFINE_string(hypervisor_uri, "qemu:///system", "Hypervisor cannonical uri.");
std::string g_default_mempath{GetPerInstanceDefault("/var/run/shm/cvd-")};
DEFINE_string(mempath,
              g_default_mempath.c_str(),
              "Target location for the shmem file.");

namespace {
void RunCommand(const char* command) {
  LOG(INFO) << "Running: " << command;
  int rval = std::system(command);
  if (rval) {
    LOG(ERROR) << "Unable to execute command: " << command;
  }
}
}  // anonymous namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  // TODO(b/78512938): Should ask the monitor to do the shutdown instead
  std::ostringstream cvd_strm;
  cvd_strm << "cvd-" << std::setfill('0') << std::setw(2) << FLAGS_instance;
  auto cvd = cvd_strm.str();
  std::string destroy_cmd = "virsh ";
  destroy_cmd += "-c ";
  destroy_cmd += FLAGS_hypervisor_uri;
  destroy_cmd += " destroy ";
  destroy_cmd += cvd;
  RunCommand(destroy_cmd.c_str());

  // TODO(b/78512938): Shouldn't need sudo to shut down
  std::string run_files = vsoc::GetDefaultPerInstanceDir() + "/*";
  std::string fuser_cmd = "sudo fuser -k ";
  fuser_cmd += run_files;
  fuser_cmd += " ";
  fuser_cmd += FLAGS_mempath;
  RunCommand(fuser_cmd.c_str());
  std::string delete_cmd = "rm -f ";
  delete_cmd += run_files;
  RunCommand(delete_cmd.c_str());
}
