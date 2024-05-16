/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <string>

#include <android-base/logging.h>
#include <gflags/gflags.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "host/commands/cvd_update_security_algorithm/update_security_algorithm_command_builder.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_int32(instance_num, cuttlefish::GetInstance(),
             "Which instance to read the configs from");
DEFINE_int32(modem_num, 0, "Which modem to send command to");
DEFINE_int32(connection_event, 0,
             "The type if connection event. See "
             "android.hardware.radio.network.ConnectionEvent");
DEFINE_int32(encryption, 0,
             "The encryption algorithm being used. See "
             "android.hardware.radio.network.SecurityAlgorithm");
DEFINE_int32(integrity, 0,
             "The integrity algorithm being used. See "
             "android.hardware.radio.network.SecurityAlgorithm");
DEFINE_bool(is_unprotected_emergency, false,
            "Whether the connection event is associated with an unprotected"
            "emergency session");

namespace cuttlefish {
namespace {

void UpdateSecurityAlgorithm(SharedFD fd) {
  std::string command = fmt::format(
      "REM{}{}", FLAGS_modem_num,
      GetATCommand(FLAGS_connection_event, FLAGS_encryption, FLAGS_integrity,
                   FLAGS_is_unprotected_emergency));

  LOG(DEBUG) << "Attempting to send command: " << command;

  long written = WriteAll(fd, command);
  if (written != command.size()) {
    LOG(FATAL) << "Failed to write data to shared fd. Tried to write "
               << command.size() << " bytes, but only wrote " << written
               << " bytes.";
  }
}

int UpdateSecurityAlgorithmMain(int argc, char **argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto config = CuttlefishConfig::Get();
  if (!config) {
    LOG(FATAL) << "Failed to obtain config object";
  }

  auto cf_config = config->ForInstance(FLAGS_instance_num);
  std::string socket_name =
      fmt::format("modem_simulator{}", cf_config.modem_simulator_host_id());

  LOG(INFO) << "Connecting over local socket: " << socket_name;
  SharedFD modem_simulator_fd =
      cuttlefish::SharedFD::SocketLocalClient(socket_name, true, SOCK_STREAM);

  UpdateSecurityAlgorithm(modem_simulator_fd);

  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char **argv) {
  return cuttlefish::UpdateSecurityAlgorithmMain(argc, argv);
}
