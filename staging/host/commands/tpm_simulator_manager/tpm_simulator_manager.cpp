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

#include <stdio.h>

#include <android-base/endian.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "host/libs/config/cuttlefish_config.h"
#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"

DEFINE_int32(port, 0, "The port to run the TPM simulator on. Consumes the next "
                      "port as well for platform commands.");

namespace {

bool HasSubstrings(const std::string& string, const std::vector<std::string>& substrings) {
  for (const auto& substr : substrings) {
    if (string.find(substr) == std::string::npos) {
      return false;
    }
  }
  return true;
}

} // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  CHECK(FLAGS_port > 0) << "A port must be set";
  auto config = vsoc::CuttlefishConfig::Get();
  CHECK(config) << "Unable to get config object";

  // Assumes linked on the host with glibc
  std::string command = "stdbuf -oL " + config->tpm_binary() + " " + std::to_string(FLAGS_port);

  LOG(INFO) << "Running: " << command;

  auto tpm_subprocess = popen(command.c_str(), "r");
  CHECK(tpm_subprocess) << "Not able to launch TPM subprocess";

  bool command_server = false;
  bool platform_server = false;
  bool sent_init = false;

  char* lineptr = nullptr;
  size_t size = 0;
  while (getline(&lineptr, &size, tpm_subprocess) >= 0) {
    std::string line(lineptr);
    if (HasSubstrings(line, {"TPM", "command", "server", "listening", "on", "port"})) {
      command_server = true;
    }
    if (HasSubstrings(line, {"Platform", "server", "listening", "on", "port"})) {
      platform_server = true;
    }
    if (command_server && platform_server && !sent_init) {
      auto client = cvd::SharedFD::SocketLocalClient(FLAGS_port + 1, SOCK_STREAM);
      std::vector<char> command_bytes(4, 0);
      *reinterpret_cast<std::uint32_t*>(command_bytes.data()) = htobe32(1); // TPM_SIGNAL_POWER_ON
      CHECK(cvd::WriteAll(client, command_bytes) == 4) << "Could not send TPM_SIGNAL_POWER_ON";
      std::vector<char> response_bytes(4, 0);
      CHECK(cvd::ReadExact(client, &response_bytes) == 4) << "Could not read parity response";

      *reinterpret_cast<std::uint32_t*>(command_bytes.data()) = htobe32(11); // TPM_SIGNAL_NV_ON
      CHECK(cvd::WriteAll(client, command_bytes) == 4) << "Could not send TPM_SIGNAL_NV_ON";
      CHECK(cvd::ReadExact(client, &response_bytes) == 4) << "Could not read parity response";

      sent_init = true;
    }
    LOG(INFO) << "TPM2SIM: " << line;
  }
  free(lineptr);

  LOG(INFO) << "TPM2 simulator stdout closed";

  return pclose(tpm_subprocess);
}
