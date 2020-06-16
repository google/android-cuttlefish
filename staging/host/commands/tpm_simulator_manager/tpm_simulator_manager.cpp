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
#include <android-base/logging.h>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"
#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"

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
  cvd::DefaultSubprocessLogging(argv);
  google::ParseCommandLineFlags(&argc, &argv, true);

  CHECK(FLAGS_port > 0) << "A port must be set";
  auto config = vsoc::CuttlefishConfig::Get();
  CHECK(config) << "Unable to get config object";

  // Assumes linked on the host with glibc
  cvd::Command simulator_cmd("/usr/bin/stdbuf");
  simulator_cmd.AddParameter("-oL");
  simulator_cmd.AddParameter(config->tpm_binary());
  simulator_cmd.AddParameter(FLAGS_port);

  cvd::SharedFD sim_stdout_in, sim_stdout_out;
  CHECK(cvd::SharedFD::Pipe(&sim_stdout_out, &sim_stdout_in))
      << "Unable to open pipe for stdout: " << strerror(errno);
  simulator_cmd.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut, sim_stdout_in);

  auto tpm_subprocess = simulator_cmd.Start();

  sim_stdout_in->Close();

  std::unique_ptr<FILE, int(*)(FILE*)> stdout_file(
      fdopen(sim_stdout_out->UNMANAGED_Dup(), "r"), &fclose);
  sim_stdout_out->Close();

  bool command_server = false;
  bool platform_server = false;
  bool sent_init = false;

  cvd::SharedFD client; // Hold this connection open for the process lifetime.

  char* lineptr = nullptr;
  size_t size = 0;
  while (getline(&lineptr, &size, stdout_file.get()) >= 0) {
    std::string line(lineptr);
    line = line.substr(0, line.size() - 1);
    if (HasSubstrings(line, {"TPM", "command", "server", "listening", "on", "port"})) {
      command_server = true;
    }
    if (HasSubstrings(line, {"Platform", "server", "listening", "on", "port"})) {
      platform_server = true;
    }
    if (command_server && platform_server && !sent_init) {
      client = cvd::SharedFD::SocketLocalClient(FLAGS_port + 1, SOCK_STREAM);
      std::uint32_t command = htobe32(1); // TPM_SIGNAL_POWER_ON
      CHECK(cvd::WriteAllBinary(client, &command) == 4)
          << "Could not send TPM_SIGNAL_POWER_ON";
      std::uint32_t response;
      CHECK(cvd::ReadExactBinary(client, &response) == 4)
          << "Could not read parity response";

      command = htobe32(11); // TPM_SIGNAL_NV_ON
      CHECK(cvd::WriteAllBinary(client, &command) == 4)
          << "Could not send TPM_SIGNAL_NV_ON";
      CHECK(cvd::ReadExactBinary(client, &response) == 4)
          << "Could not read parity response";

      sent_init = true;
    }
    LOG(INFO) << "TPM2SIM: " << line;
  }
  free(lineptr);

  return tpm_subprocess.Wait() ? 0 : 1;
}
