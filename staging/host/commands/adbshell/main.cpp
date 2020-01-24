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

/* Utility that uses an adb connection as the login shell. */

#include "host/libs/config/cuttlefish_config.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <errno.h>
#include <unistd.h>

// Many of our users interact with CVDs via ssh. They expect to be able to
// get an Android shell (as opposed to the host shell) with a single command.
//
// Our goals are to:
//
//   * Allow the user to select which CVD to connect to
//
//   * Avoid modifications to the host-side sshd and the protocol
//
// We accomplish this by using specialized accounts: vsoc-## and cvd-## and
// specific Android serial numbers:
//
//    The vsoc-01 account provides a host-side shell that controls the first CVD
//    The cvd-01 account is connected to the Andorid shell of the first CVD
//    The first CVD has a serial number of CUTTLEFISHCVD01
//
// The code in the commands/launch directory also follows these conventions by
// default.
//

namespace {
std::string VsocUser() {
  const char* user_cstring = std::getenv("USER");
  assert(user_cstring != nullptr);
  std::string user(user_cstring);

  std::string cvd_prefix = "cvd-";
  if (user.find(cvd_prefix) == 0) {
    user.replace(0, cvd_prefix.size(), vsoc::kVsocUserPrefix);
  }
  return user;
}

std::string CuttlefishConfigLocation() {
  return std::string("/home/") + VsocUser() +
         "/cuttlefish_runtime/cuttlefish_config.json";
}

std::string CuttlefishFindAdb() {
  std::string rval = std::string("/home/") + VsocUser() + "/bin/adb";
  if (TEMP_FAILURE_RETRY(access(rval.c_str(), X_OK)) == -1) {
    return "/usr/bin/adb";
  }
  return rval;
}

void SetCuttlefishConfigEnv() {
  setenv(vsoc::kCuttlefishConfigEnvVarName, CuttlefishConfigLocation().c_str(),
         true);
}
}  // namespace

int main(int argc, char* argv[]) {
  SetCuttlefishConfigEnv();
  auto instance = vsoc::CuttlefishConfig::Get()
      ->ForDefaultInstance().adb_device_name();
  std::string adb_path = CuttlefishFindAdb();

  std::vector<char*> new_argv = {
      const_cast<char*>(adb_path.c_str()), const_cast<char*>("-s"),
      const_cast<char*>(instance.c_str()), const_cast<char*>("shell"),
      const_cast<char*>("/system/bin/sh")};

  // Some important data is lost before this point, and there are
  // no great recovery options:
  // * ssh with no arguments comes in with 1 arg of -adbshell. The command
  //   given above does the right thing if we don't invoke the shell.
  if (argc == 1) {
    new_argv.back() = nullptr;
  }
  // * simple shell commands come in with a -c and a single string. The
  //   problem here is that adb doesn't preserve spaces, so we need
  //   to do additional escaping. The best compromise seems to be to
  //   throw double quotes around each string.
  for (int i = 1; i < argc; ++i) {
    size_t buf_size = std::strlen(argv[i]) + 4;
    new_argv.push_back(new char[buf_size]);
    std::snprintf(new_argv.back(), buf_size, "\"%s\"", argv[i]);
  }
  //
  // * scp seems to be pathologically broken when paths contain spaces.
  //   spaces aren't properly escaped by gcloud, so scp will fail with
  //   "scp: with ambiguous target." We might be able to fix this with
  //   some creative parsing of the arguments, but that seems like
  //   overkill.
  new_argv.push_back(nullptr);
  execv(new_argv[0], new_argv.data());
  // This never should happen
  return 2;
}
