//
// Copyright (C) 2019 The Android Open Source Project
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

#include "host/commands/run_cvd/launch.h"

#include <android-base/logging.h>
#include <string.h>
#include <sstream>
#include <string>
#include <utility>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

static bool StopModemSimulator() {
  auto config = CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();

  std::string monitor_socket_name = "modem_simulator";
  std::stringstream ss;
  ss << instance.host_port();
  monitor_socket_name.append(ss.str());
  auto monitor_sock = SharedFD::SocketLocalClient(monitor_socket_name.c_str(),
                                                  true, SOCK_STREAM);
  if (!monitor_sock->IsOpen()) {
    LOG(ERROR) << "The connection to modem simulator is closed";
    return false;
  }
  std::string msg("STOP");
  if (monitor_sock->Write(msg.data(), msg.size()) < 0) {
    monitor_sock->Close();
    LOG(ERROR) << "Failed to send 'STOP' to modem simulator";
    return false;
  }
  char buf[64] = {0};
  if (monitor_sock->Read(buf, sizeof(buf)) <= 0) {
    monitor_sock->Close();
    LOG(ERROR) << "Failed to read message from modem simulator";
    return false;
  }
  if (strcmp(buf, "OK")) {
    monitor_sock->Close();
    LOG(ERROR) << "Read '" << buf << "' instead of 'OK' from modem simulator";
    return false;
  }

  return true;
}

std::vector<Command> LaunchModemSimulatorIfEnabled(
    const CuttlefishConfig& config) {
  if (!config.enable_modem_simulator()) {
    LOG(DEBUG) << "Modem simulator not enabled";
    return {};
  }

  int instance_number = config.modem_simulator_instance_number();
  if (instance_number > 3 /* max value */ || instance_number < 0) {
    LOG(ERROR)
        << "Modem simulator instance number should range between 1 and 3";
    return {};
  }

  Command cmd(ModemSimulatorBinary(), [](Subprocess* proc) {
    auto stopped = StopModemSimulator();
    if (stopped) {
      return true;
    }
    LOG(WARNING) << "Failed to stop modem simulator nicely, "
                 << "attempting to KILL";
    return KillSubprocess(proc);
  });

  auto sim_type = config.modem_simulator_sim_type();
  cmd.AddParameter(std::string{"-sim_type="} + std::to_string(sim_type));

  auto instance = config.ForDefaultInstance();
  auto ports = instance.modem_simulator_ports();
  cmd.AddParameter("-server_fds=");
  for (int i = 0; i < instance_number; ++i) {
    auto pos = ports.find(',');
    auto temp = (pos != std::string::npos) ? ports.substr(0, pos) : ports;
    auto port = std::stoi(temp);
    ports = ports.substr(pos + 1);

    auto socket = SharedFD::VsockServer(port, SOCK_STREAM);
    CHECK(socket->IsOpen())
        << "Unable to create modem simulator server socket: "
        << socket->StrError();
    if (i > 0) {
      cmd.AppendToLastParameter(",");
    }
    cmd.AppendToLastParameter(socket);
  }

  std::vector<Command> commands;
  commands.emplace_back(std::move(cmd));
  return commands;
}

}  // namespace cuttlefish
