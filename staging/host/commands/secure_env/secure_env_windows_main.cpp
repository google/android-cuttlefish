//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "host/commands/secure_env/secure_env_windows_lib.h"

DEFINE_string(keymaster_pipe, "", "Keymaster pipe path");
DEFINE_string(gatekeeper_pipe, "", "Gatekeeper pipe path");
DEFINE_bool(use_tpm, false, "Whether to use TPM for cryptography primitives.");

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string keymaster_pipe = FLAGS_keymaster_pipe;
  if (keymaster_pipe.empty()) {
    LOG(FATAL) << "Keymaster pipe (--keymaster_pipe) not specified.";
  }
  std::string gatekeeper_pipe = FLAGS_gatekeeper_pipe;
  if (gatekeeper_pipe.empty()) {
    LOG(FATAL) << "Gatekeeper pipe (--gatekeeper_pipe) not specified.";
  }

  bool use_tpm = FLAGS_use_tpm;
  if (keymaster_pipe.empty() || gatekeeper_pipe.empty()) {
    LOG(ERROR) << "Invalid arguments. See --help for details.";
    return 1;
  }

  // Start up secure_env and wait for its threads to exit before returning.
  if (!secure_env::StartSecureEnv(keymaster_pipe.c_str(),
                                  gatekeeper_pipe.c_str(), use_tpm)) {
    return 1;
  }

  return 0;
}