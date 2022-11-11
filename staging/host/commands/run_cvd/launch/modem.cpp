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

#include "host/commands/run_cvd/launch/launch.h"

#include <android-base/logging.h>
#include <string.h>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

static bool StopModemSimulator(int id) {
  std::string socket_name = "modem_simulator" + std::to_string(id);
  auto monitor_sock =
      SharedFD::SocketLocalClient(socket_name, true, SOCK_STREAM);
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
  INJECT(ModemSimulator(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // CommandSource
  Result<std::vector<Command>> Commands() override {
    Command cmd(ModemSimulatorBinary(), [this](Subprocess* proc) {
      auto stopped = StopModemSimulator(instance_.modem_simulator_host_id());
      if (stopped) {
        return StopperResult::kStopSuccess;
      }
      LOG(WARNING) << "Failed to stop modem simulator nicely, "
                   << "attempting to KILL";
      return KillSubprocess(proc) == StopperResult::kStopSuccess
                 ? StopperResult::kStopCrash
                 : StopperResult::kStopFailure;
    });

    auto sim_type = instance_.modem_simulator_sim_type();
    cmd.AddParameter(std::string{"-sim_type="} + std::to_string(sim_type));
    cmd.AddParameter("-server_fds=");
    bool first_socket = true;
    for (const auto& socket : sockets_) {
      if (!first_socket) {
        cmd.AppendToLastParameter(",");
      }
      cmd.AppendToLastParameter(socket);
      first_socket = false;
    }

    std::vector<Command> commands;
    commands.emplace_back(std::move(cmd));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "ModemSimulator"; }
  bool Enabled() const override {
    if (!instance_.enable_modem_simulator()) {
      LOG(DEBUG) << "Modem simulator not enabled";
    }
    return instance_.enable_modem_simulator();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    int instance_number = instance_.modem_simulator_instance_number();
    CF_EXPECT(instance_number >= 0 && instance_number < 4,
              "Modem simulator instance number should range between 0 and 3");
    auto ports = instance_.modem_simulator_ports();
    for (int i = 0; i < instance_number; ++i) {
      auto pos = ports.find(',');
      auto temp = (pos != std::string::npos) ? ports.substr(0, pos) : ports;
      auto port = std::stoi(temp);
      ports = ports.substr(pos + 1);

      auto modem_sim_socket = SharedFD::VsockServer(port, SOCK_STREAM);
      CF_EXPECT(modem_sim_socket->IsOpen(), modem_sim_socket->StrError());
      sockets_.emplace_back(std::move(modem_sim_socket));
    }
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
  std::vector<SharedFD> sockets_;
};

fruit::Component<fruit::Required<const CuttlefishConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
launchModemComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, ModemSimulator>()
      .addMultibinding<SetupFeature, ModemSimulator>();
}

}  // namespace cuttlefish
