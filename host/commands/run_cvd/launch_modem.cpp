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

static bool StopModemSimulator(int host_port) {
  std::string socket_name = "modem_simulator" + std::to_string(host_port);
  auto monitor_sock =
      SharedFD::SocketLocalClient(socket_name.c_str(), true, SOCK_STREAM);
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

class ModemSimulator : public CommandSource {
 public:
  INJECT(ModemSimulator(const CuttlefishConfig& config,
                        const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  std::vector<Command> Commands() override {
    if (!config_.enable_modem_simulator()) {
      LOG(DEBUG) << "Modem simulator not enabled";
      return {};
    }

    int instance_number = config_.modem_simulator_instance_number();
    if (instance_number > 3 /* max value */ || instance_number < 0) {
      LOG(ERROR)
          << "Modem simulator instance number should range between 1 and 3";
      return {};
    }

    Command cmd(ModemSimulatorBinary(), [this](Subprocess* proc) {
      auto stopped = StopModemSimulator(instance_.host_port());
      if (stopped) {
        return true;
      }
      LOG(WARNING) << "Failed to stop modem simulator nicely, "
                   << "attempting to KILL";
      return KillSubprocess(proc);
    });

    auto sim_type = config_.modem_simulator_sim_type();
    cmd.AddParameter(std::string{"-sim_type="} + std::to_string(sim_type));

    auto ports = instance_.modem_simulator_ports();
    cmd.AddParameter("-server_fds=");
    for (int i = 0; i < instance_number; ++i) {
      auto pos = ports.find(',');
      auto temp = (pos != std::string::npos) ? ports.substr(0, pos - 1) : ports;
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

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
launchModemComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, ModemSimulator>();
}

}  // namespace cuttlefish
