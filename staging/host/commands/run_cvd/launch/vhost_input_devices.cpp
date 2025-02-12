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

#include <regex>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <fruit/fruit.h>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/launch/input_connections_provider.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

using Subprocess::StdIOChannel::kStdErr;

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

struct TemplateVars {
  int index;
  int width;
  int height;
};

std::string BuildTouchSpec(const std::string& spec_template,
                           TemplateVars vars) {
  std::pair<std::string, int> replacements[] = {{"%INDEX%", vars.index},
                                                {"%WIDTH%", vars.width},
                                                {"%HEIGHT%", vars.height}};
  std::string spec = spec_template;
  for (const auto& [key, value] : replacements) {
    spec = std::regex_replace(spec, std::regex(key), std::to_string(value));
  }
  return spec;
}

// Creates the commands for the vhost user input devices.
class VhostInputDevices : public CommandSource,
                          public InputConnectionsProvider {
 public:
  INJECT(VhostInputDevices(const CuttlefishConfig::InstanceSpecific& instance,
                           LogTeeCreator& log_tee))
      : instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    std::vector<MonitorCommand> commands;
    Command rotary_cmd =
        NewVhostUserInputCommand(rotary_sockets_, DefaultRotaryDeviceSpec());
    Command rotary_log_tee = CF_EXPECT(
        log_tee_.CreateLogTee(rotary_cmd, "vhost_user_rotary", kStdErr),
        "Failed to create log tee command for rotary device");
    commands.emplace_back(std::move(rotary_cmd));
    commands.emplace_back(std::move(rotary_log_tee));

    if (instance_.enable_mouse()) {
      Command mouse_cmd =
          NewVhostUserInputCommand(mouse_sockets_, DefaultMouseSpec());
      Command mouse_log_tee = CF_EXPECT(
          log_tee_.CreateLogTee(mouse_cmd, "vhost_user_mouse", kStdErr),
          "Failed to create log tee command for mouse device");
      commands.emplace_back(std::move(mouse_cmd));
      commands.emplace_back(std::move(mouse_log_tee));
    }

    std::string keyboard_spec =
        instance_.custom_keyboard_config().value_or(DefaultKeyboardSpec());
    Command keyboard_cmd =
        NewVhostUserInputCommand(keyboard_sockets_, keyboard_spec);
    Command keyboard_log_tee = CF_EXPECT(
        log_tee_.CreateLogTee(keyboard_cmd, "vhost_user_keyboard", kStdErr),
        "Failed to create log tee command for keyboard device");
    commands.emplace_back(std::move(keyboard_cmd));
    commands.emplace_back(std::move(keyboard_log_tee));

    Command switches_cmd =
        NewVhostUserInputCommand(switches_sockets_, DefaultSwitchesSpec());
    Command switches_log_tee = CF_EXPECT(
        log_tee_.CreateLogTee(switches_cmd, "vhost_user_switches", kStdErr),
        "Failed to create log tee command for switches device");
    commands.emplace_back(std::move(switches_cmd));
    commands.emplace_back(std::move(switches_log_tee));

    const bool use_multi_touch =
        instance_.guest_os() !=
        CuttlefishConfig::InstanceSpecific::GuestOs::ChromeOs;

    std::string touchscreen_template_path =
        use_multi_touch ? DefaultMultiTouchscreenSpecTemplate()
                        : DefaultSingleTouchscreenSpecTemplate();
    const std::string touchscreen_template = CF_EXPECTF(
        ReadFileContents(touchscreen_template_path),
        "Failed to load touchscreen template: {}", touchscreen_template_path);
    for (int i = 0; i < instance_.display_configs().size(); ++i) {
      const int width = instance_.display_configs()[i].width;
      const int height = instance_.display_configs()[i].height;
      const std::string spec = BuildTouchSpec(
          touchscreen_template, {.index = i, .width = width, .height = height});
      const std::string spec_path = instance_.PerInstanceInternalPath(
          fmt::format("touchscreen_spec_{}", i));
      CF_EXPECTF(android::base::WriteStringToFile(spec, spec_path,
                                                  true /*follow symlinks*/),
                 "Failed to write touchscreen spec to file: {}", spec_path);
      Command touchscreen_cmd =
          NewVhostUserInputCommand(touchscreen_sockets_[i], spec_path);
      Command touchscreen_log_tee =
          CF_EXPECTF(log_tee_.CreateLogTee(
                         touchscreen_cmd,
                         fmt::format("vhost_user_touchscreen_{}", i), kStdErr),
                     "Failed to create log tee for touchscreen device", i);
      commands.emplace_back(std::move(touchscreen_cmd));
      commands.emplace_back(std::move(touchscreen_log_tee));
    }

    std::string touchpad_template_path =
        use_multi_touch ? DefaultMultiTouchpadSpecTemplate()
                        : DefaultSingleTouchpadSpecTemplate();
    const std::string touchpad_template = CF_EXPECTF(
        ReadFileContents(touchpad_template_path),
        "Failed to load touchpad template: {}", touchpad_template_path);
    for (int i = 0; i < instance_.touchpad_configs().size(); ++i) {
      const int width = instance_.touchpad_configs()[i].width;
      const int height = instance_.touchpad_configs()[i].height;
      const std::string spec = BuildTouchSpec(
          touchpad_template, {.index = i, .width = width, .height = height});
      const std::string spec_path =
          instance_.PerInstanceInternalPath(fmt::format("touchpad_spec_{}", i));
      CF_EXPECTF(android::base::WriteStringToFile(spec, spec_path,
                                                  true /*follow symlinks*/),
                 "Failed to write touchpad spec to file: {}", spec_path);
      Command touchpad_cmd =
          NewVhostUserInputCommand(touchpad_sockets_[i], spec_path);
      Command touchpad_log_tee = CF_EXPECTF(
          log_tee_.CreateLogTee(
              touchpad_cmd, fmt::format("vhost_user_touchpad_{}", i), kStdErr),
          "Failed to create log tee for touchpad {}", i);
      commands.emplace_back(std::move(touchpad_cmd));
      commands.emplace_back(std::move(touchpad_log_tee));
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

  SharedFD KeyboardConnection() const override {
    return keyboard_sockets_.streamer_end;
  }

  SharedFD SwitchesConnection() const override {
    return switches_sockets_.streamer_end;
  }

  std::vector<SharedFD> TouchscreenConnections() const override {
    std::vector<SharedFD> conns;
    conns.reserve(touchscreen_sockets_.size());
    for (const DeviceSockets& sockets : touchscreen_sockets_) {
      conns.emplace_back(sockets.streamer_end);
    }
    return conns;
  }

  std::vector<SharedFD> TouchpadConnections() const override {
    std::vector<SharedFD> conns;
    conns.reserve(touchpad_sockets_.size());
    for (const DeviceSockets& sockets : touchpad_sockets_) {
      conns.emplace_back(sockets.streamer_end);
    }
    return conns;
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
    keyboard_sockets_ =
        CF_EXPECT(NewDeviceSockets(instance_.keyboard_socket_path()),
                  "Failed to setup sockets for keyboard device");
    switches_sockets_ =
        CF_EXPECT(NewDeviceSockets(instance_.switches_socket_path()),
                  "Failed to setup sockets for switches device");
    touchscreen_sockets_.reserve(instance_.display_configs().size());
    for (int i = 0; i < instance_.display_configs().size(); ++i) {
      touchscreen_sockets_.emplace_back(
          CF_EXPECTF(NewDeviceSockets(instance_.touch_socket_path(i)),
                     "Failed to setup sockets for touchscreen {}", i));
    }
    touchpad_sockets_.reserve(instance_.touchpad_configs().size());
    for (int i = 0; i < instance_.touchpad_configs().size(); ++i) {
      int idx = touchscreen_sockets_.size() + i;
      touchpad_sockets_.emplace_back(
          CF_EXPECTF(NewDeviceSockets(instance_.touch_socket_path(idx)),
                     "Failed to setup sockets for touchpad {}", i));
    }
    return {};
  }

  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
  DeviceSockets rotary_sockets_;
  DeviceSockets mouse_sockets_;
  DeviceSockets keyboard_sockets_;
  DeviceSockets switches_sockets_;
  std::vector<DeviceSockets> touchscreen_sockets_;
  std::vector<DeviceSockets> touchpad_sockets_;
};

}  // namespace
fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InputConnectionsProvider, LogTeeCreator>
VhostInputDevicesComponent() {
  return fruit::createComponent()
      .bind<InputConnectionsProvider, VhostInputDevices>()
      .addMultibinding<CommandSource, VhostInputDevices>()
      .addMultibinding<SetupFeature, VhostInputDevices>();
}

}  // namespace cuttlefish
