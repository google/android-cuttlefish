//
// Copyright (C) 2024 The Android Open Source Project
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

#include <sys/socket.h>

#include <vector>

#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/launch/input_connections_provider.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

// Holds all sockets related to a single vhost user input device process.
struct DeviceSockets {
  // Device end of the connection between device and streamer.
  SharedFD device_end;
  // Streamer end of the connection between device and streamer.
  SharedFD streamer_end;
  // Unix socket for the server to which the VMM connects to. It's created and
  // held at the CommandSource level to ensure it already exists by the time the
  // VMM runs and attempts to connect.
  SharedFD vhu_server;
};

Result<DeviceSockets> NewDeviceSockets(const std::string& vhu_server_path) {
  DeviceSockets ret;
  CF_EXPECTF(
      SharedFD::SocketPair(AF_UNIX, SOCK_STREAM, 0, &ret.device_end,
                           &ret.streamer_end),
      "Failed to create connection sockets (socket pair) for input device: {}",
      ret.device_end->StrError());

  // The webRTC process currently doesn't read status updates from input
  // devices, so the vhost processes will write that to /dev/null.
  // These calls shouldn't return errors since we already know these are a newly
  // created socket pair.
  CF_EXPECTF(ret.device_end->Shutdown(SHUT_WR) == 0,
             "Failed to close input connection's device for writes: {}",
             ret.device_end->StrError());
  CF_EXPECTF(ret.streamer_end->Shutdown(SHUT_RD) == 0,
             "Failed to close input connection's streamer end for reads: {}",
             ret.streamer_end->StrError());

  ret.vhu_server =
      SharedFD::SocketLocalServer(vhu_server_path, false, SOCK_STREAM, 0600);
  CF_EXPECTF(ret.vhu_server->IsOpen(),
             "Failed to create vhost user socket for device: {}",
             ret.vhu_server->StrError());

  return ret;
}

Command NewVhostUserInputCommand(const DeviceSockets& device_sockets,
                            const std::string& spec) {
  Command cmd(VhostUserInputBinary());
  cmd.AddParameter("--verbosity=DEBUG");
  cmd.AddParameter("--socket-fd=", device_sockets.vhu_server);
  cmd.AddParameter("--device-config=", spec);
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn,
                    device_sockets.device_end);
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut,
                    SharedFD::Open("/dev/null", O_WRONLY));
  return cmd;
}

// Creates the commands for the vhost user input devices.
class VhostInputDevices : public CommandSource,
                          public InputConnectionsProvider {
 public:
  INJECT(VhostInputDevices(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    std::vector<MonitorCommand> commands;
    commands.emplace_back(
        NewVhostUserInputCommand(rotary_sockets_, DefaultRotaryDeviceSpec()));
    if (instance_.enable_mouse()) {
      commands.emplace_back(
          NewVhostUserInputCommand(mouse_sockets_, DefaultMouseSpec()));
    }

    return commands;
  }

  // InputConnectionsProvider
  SharedFD RotaryDeviceConnection() const override {
    return rotary_sockets_.streamer_end;
  }

  SharedFD MouseConnection() const override {
    return mouse_sockets_.streamer_end;
  }

 private:
  // SetupFeature
  std::string Name() const override { return "VhostInputDevices"; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    rotary_sockets_ =
        CF_EXPECT(NewDeviceSockets(instance_.rotary_socket_path()),
                  "Failed to setup sockets for rotary device");
    if (instance_.enable_mouse()) {
      mouse_sockets_ =
          CF_EXPECT(NewDeviceSockets(instance_.mouse_socket_path()),
                    "Failed to setup sockets for mouse device");
    }
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
  DeviceSockets rotary_sockets_;
  DeviceSockets mouse_sockets_;
};

}  // namespace
fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InputConnectionsProvider>
VhostInputDevicesComponent() {
  return fruit::createComponent()
      .bind<InputConnectionsProvider, VhostInputDevices>()
      .addMultibinding<CommandSource, VhostInputDevices>()
      .addMultibinding<SetupFeature, VhostInputDevices>();
}

}  // namespace cuttlefish
