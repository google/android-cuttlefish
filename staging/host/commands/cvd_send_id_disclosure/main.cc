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
#include "host/commands/cvd_send_id_disclosure/cellular_identifier_disclosure_command_builder.h"
#include "host/libs/config/cuttlefish_config.h"

DEFINE_int32(instance_num, cuttlefish::GetInstance(),
             "Which instance to read the configs from");
DEFINE_int32(modem_num, 0, "Which modem to send command to");
DEFINE_int32(identifier, 1,
             "The identifier type that was disclosed. See "
             "android.hardware.radio.network.CellularIdentifier");
DEFINE_int32(protocol_message, 1,
             "The protocol message of the disclosure. See "
             "android.hardware.radio.network.NasProtocolMessage");
DEFINE_bool(is_emergency, false,
            "Whether or not this disclosure occurred during an emergency call");
DEFINE_string(plmn, "001001",
              "The PLMN of the network on which the identifier was disclosed");

namespace cuttlefish {
namespace {

void SendDisclosure(SharedFD fd) {
  std::string command =
      fmt::format("REM{}{}", FLAGS_modem_num,
                  GetATCommand(FLAGS_plmn, FLAGS_identifier,
                               FLAGS_protocol_message, FLAGS_is_emergency));

  LOG(DEBUG) << "Attempting to send command: " << command;

  long written = WriteAll(fd, command);
  if (written != command.size()) {
    LOG(FATAL) << "Failed to write data to shared fd. Tried to write "
               << command.size() << " bytes, but only wrote " << written
               << " bytes.";
  }
}

int SendIdDisclosureMain(int argc, char **argv) {
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

  SendDisclosure(modem_simulator_fd);

  return 0;
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char **argv) {
  return cuttlefish::SendIdDisclosureMain(argc, argv);
}