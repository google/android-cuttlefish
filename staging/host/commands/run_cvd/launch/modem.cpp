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

#include <string.h>

#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

static StopperResult StopModemSimulator(int id) {
  std::string socket_name = "modem_simulator" + std::to_string(id);
  auto monitor_sock =
      SharedFD::SocketLocalClient(socket_name, true, SOCK_STREAM);
  if (!monitor_sock->IsOpen()) {
    LOG(ERROR) << "The connection to modem simulator is closed";
    return StopperResult::kStopFailure;
  }
  std::string msg("STOP");
  if (monitor_sock->Write(msg.data(), msg.size()) < 0) {
    monitor_sock->Close();
    LOG(ERROR) << "Failed to send 'STOP' to modem simulator";
    return StopperResult::kStopFailure;
  }
  char buf[64] = {0};
  if (monitor_sock->Read(buf, sizeof(buf)) <= 0) {
    monitor_sock->Close();
    LOG(ERROR) << "Failed to read message from modem simulator";
    return StopperResult::kStopFailure;
  }
  if (strcmp(buf, "OK")) {
    monitor_sock->Close();
    LOG(ERROR) << "Read '" << buf << "' instead of 'OK' from modem simulator";
    return StopperResult::kStopFailure;
  }

  return StopperResult::kStopSuccess;
}

Result<std::optional<MonitorCommand>> ModemSimulator(
    const CuttlefishConfig::InstanceSpecific& instance) {
  if (!instance.enable_modem_simulator()) {
    LOG(DEBUG) << "Modem simulator not enabled";
    return {};
  }
  int instance_number = instance.modem_simulator_instance_number();
  CF_EXPECT(instance_number >= 0 && instance_number < 4,
            "Modem simulator instance number should range between 0 and 3");
  auto ports = instance.modem_simulator_ports();
  std::vector<SharedFD> sockets;
  for (int i = 0; i < instance_number; ++i) {
    auto pos = ports.find(',');
    auto temp = (pos != std::string::npos) ? ports.substr(0, pos) : ports;
    auto port = std::stoi(temp);
    ports = ports.substr(pos + 1);

    auto modem_sim_socket = SharedFD::VsockServer(
        port, SOCK_STREAM,
        instance.vhost_user_vsock()
            ? std::make_optional(instance.vsock_guest_cid())
            : std::nullopt);
    CF_EXPECT(
        modem_sim_socket->IsOpen(),
        modem_sim_socket->StrError()
            << " (try `cvd reset`, or `pkill run_cvd` and `pkill crosvm`)");
    sockets.emplace_back(std::move(modem_sim_socket));
  }

  auto id = instance.modem_simulator_host_id();
  auto nice_stop = [id]() { return StopModemSimulator(id); };
  Command cmd(ModemSimulatorBinary(), KillSubprocessFallback(nice_stop));

  auto sim_type = instance.modem_simulator_sim_type();
  cmd.AddParameter(std::string{"-sim_type="} + std::to_string(sim_type));
  cmd.AddParameter("-server_fds=");
  bool first_socket = true;
  for (const auto& socket : sockets) {
    if (!first_socket) {
      cmd.AppendToLastParameter(",");
    }
    cmd.AppendToLastParameter(socket);
    first_socket = false;
  }
  return cmd;
}

}  // namespace cuttlefish
