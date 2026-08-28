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

#include "cuttlefish/host/commands/run_cvd/launch/input_paths_provider.h"

#include <sys/socket.h>  // IWYU pragma: keep: SOCK_STREAM

#include <regex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "android-base/file.h"
#include "fmt/core.h"
#include "fmt/format.h"
#include "fruit/component.h"
#include "fruit/fruit_forward_decls.h"
#include "fruit/macro.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/commands/run_cvd/launch/enable_multitouch.h"
#include "cuttlefish/host/commands/run_cvd/launch/log_tee_creator.h"
#include "cuttlefish/host/libs/config/config_instance_derived.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// Holds all sockets related to a single vhost user input device process.
struct DeviceSockets {
  // Path to the events server socket.
  std::string events_server_path;
  // The events server fd. It's created and held at the CommandSource level to
  // ensure it already exists by the time the streamer attempts to connect.
  SharedFD events_server;
  // Unix socket for the server to which the VMM connects to. It's created and
  // held at the CommandSource level to ensure it already exists by the time the
  // VMM runs and attempts to connect.
  SharedFD vhu_server;
};

Result<DeviceSockets> NewDeviceSockets(const std::string& evt_server_path,
                                       const std::string& vhu_server_path) {
  DeviceSockets ret{
      .events_server_path = evt_server_path,
      .events_server = CF_EXPECT(
          Fd::SocketLocalServer(evt_server_path, false, SOCK_STREAM, 0600),
          "Failed to create event server for device"),
      .vhu_server = CF_EXPECT(
          Fd::SocketLocalServer(vhu_server_path, false, SOCK_STREAM, 0600),
          "Failed to create vhost user socket for device"),
  };

  return ret;
}

Command NewVhostUserInputCommand(const DeviceSockets& device_sockets,
                                 const std::string& spec) {
  Command cmd(VhostUserInputBinary());
  cmd.AddParameter("--verbosity=DEBUG");
  cmd.AddParameter("--socket-fd=", device_sockets.vhu_server);
  cmd.AddParameter("--device-config=", spec);
  cmd.AddParameter("--server-fd=", device_sockets.events_server);
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
class VhostInputDevices : public CommandSource, public InputPathsProvider {
 public:
  INJECT(VhostInputDevices(const CuttlefishConfig::InstanceSpecific& instance,
                           LogTeeCreator& log_tee))
      : instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    std::vector<MonitorCommand> commands;
    Command rotary_cmd =
        NewVhostUserInputCommand(rotary_sockets_, DefaultRotaryDeviceSpec());
    Command rotary_log_tee =
        CF_EXPECT(log_tee_.CreateLogTee(rotary_cmd, "vhost_user_rotary",
                                        Command::StdIoChannel::kStdErr),
                  "Failed to create log tee command for rotary device");
    commands.emplace_back(std::move(rotary_cmd));
    commands.emplace_back(std::move(rotary_log_tee));

    if (instance_.enable_mouse()) {
      Command mouse_cmd =
          NewVhostUserInputCommand(mouse_sockets_, DefaultMouseSpec());
      Command mouse_log_tee =
          CF_EXPECT(log_tee_.CreateLogTee(mouse_cmd, "vhost_user_mouse",
                                          Command::StdIoChannel::kStdErr),
                    "Failed to create log tee command for mouse device");
      commands.emplace_back(std::move(mouse_cmd));
      commands.emplace_back(std::move(mouse_log_tee));
    }

    if (instance_.enable_gamepad()) {
      Command gamepad_cmd =
          NewVhostUserInputCommand(gamepad_sockets_, DefaultGamepadSpec());
      Command gamepad_log_tee =
          CF_EXPECT(log_tee_.CreateLogTee(gamepad_cmd, "vhost_user_gamepad",
                                          Command::StdIoChannel::kStdErr),
                    "Failed to create log tee command for gamepad device");
      commands.emplace_back(std::move(gamepad_cmd));
      commands.emplace_back(std::move(gamepad_log_tee));
    }

    std::string keyboard_spec =
        instance_.custom_keyboard_config().value_or(DefaultKeyboardSpec());
    Command keyboard_cmd =
        NewVhostUserInputCommand(keyboard_sockets_, keyboard_spec);
    Command keyboard_log_tee =
        CF_EXPECT(log_tee_.CreateLogTee(keyboard_cmd, "vhost_user_keyboard",
                                        Command::StdIoChannel::kStdErr),
                  "Failed to create log tee command for keyboard device");
    commands.emplace_back(std::move(keyboard_cmd));
    commands.emplace_back(std::move(keyboard_log_tee));

    Command switches_cmd =
        NewVhostUserInputCommand(switches_sockets_, DefaultSwitchesSpec());
    Command switches_log_tee =
        CF_EXPECT(log_tee_.CreateLogTee(switches_cmd, "vhost_user_switches",
                                        Command::StdIoChannel::kStdErr),
                  "Failed to create log tee command for switches device");
    commands.emplace_back(std::move(switches_cmd));
    commands.emplace_back(std::move(switches_log_tee));

    const bool use_multi_touch = ShouldEnableMultitouch(instance_);

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
      Command touchscreen_log_tee = CF_EXPECTF(
          log_tee_.CreateLogTee(touchscreen_cmd,
                                fmt::format("vhost_user_touchscreen_{}", i),
                                Command::StdIoChannel::kStdErr),
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
      Command touchpad_log_tee =
          CF_EXPECTF(log_tee_.CreateLogTee(
                         touchpad_cmd, fmt::format("vhost_user_touchpad_{}", i),
                         Command::StdIoChannel::kStdErr),
                     "Failed to create log tee for touchpad {}", i);
      commands.emplace_back(std::move(touchpad_cmd));
      commands.emplace_back(std::move(touchpad_log_tee));
    }
    return commands;
  }

  // InputPathsProvider
  std::string RotaryDevicePath() const override {
    return rotary_sockets_.events_server_path;
  }

  std::string MousePath() const override {
    return mouse_sockets_.events_server_path;
  }

  std::string GamepadPath() const override {
    return gamepad_sockets_.events_server_path;
  }

  std::string KeyboardPath() const override {
    return keyboard_sockets_.events_server_path;
  }

  std::string SwitchesPath() const override {
    return switches_sockets_.events_server_path;
  }

  std::vector<std::string> TouchscreenPaths() const override {
    std::vector<std::string> conns;
    conns.reserve(touchscreen_sockets_.size());
    for (const DeviceSockets& sockets : touchscreen_sockets_) {
      conns.emplace_back(sockets.events_server_path);
    }
    return conns;
  }

  std::vector<std::string> TouchpadPaths() const override {
    std::vector<std::string> conns;
    conns.reserve(touchpad_sockets_.size());
    for (const DeviceSockets& sockets : touchpad_sockets_) {
      conns.emplace_back(sockets.events_server_path);
    }
    return conns;
  }

 private:
  // SetupFeature
  std::string Name() const override { return "VhostInputDevices"; }
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    rotary_sockets_ =
        CF_EXPECT(NewDeviceSockets(RotaryEventsServerPath(instance_),
                                   RotarySocketPath(instance_)),
                  "Failed to setup sockets for rotary device");
    if (instance_.enable_mouse()) {
      mouse_sockets_ =
          CF_EXPECT(NewDeviceSockets(MouseEventsServerPath(instance_),
                                     MouseSocketPath(instance_)),
                    "Failed to setup sockets for mouse device");
    }
    if (instance_.enable_gamepad()) {
      gamepad_sockets_ =
          CF_EXPECT(NewDeviceSockets(GamepadEventsServerPath(instance_),
                                     GamepadSocketPath(instance_)),
                    "Failed to setup sockets for gamepad device");
    }
    keyboard_sockets_ =
        CF_EXPECT(NewDeviceSockets(KeyboardEventsServerPath(instance_),
                                   KeyboardSocketPath(instance_)),
                  "Failed to setup sockets for keyboard device");
    switches_sockets_ =
        CF_EXPECT(NewDeviceSockets(SwitchesEventsServerPath(instance_),
                                   SwitchesSocketPath(instance_)),
                  "Failed to setup sockets for switches device");
    touchscreen_sockets_.reserve(instance_.display_configs().size());
    for (int i = 0; i < instance_.display_configs().size(); ++i) {
      touchscreen_sockets_.emplace_back(
          CF_EXPECTF(NewDeviceSockets(instance_.touch_events_server_path(i),
                                      instance_.touch_socket_path(i)),
                     "Failed to setup sockets for touchscreen {}", i));
    }
    touchpad_sockets_.reserve(instance_.touchpad_configs().size());
    for (int i = 0; i < instance_.touchpad_configs().size(); ++i) {
      int idx = touchscreen_sockets_.size() + i;
      touchpad_sockets_.emplace_back(
          CF_EXPECTF(NewDeviceSockets(instance_.touch_events_server_path(idx),
                                      instance_.touch_socket_path(idx)),
                     "Failed to setup sockets for touchpad {}", i));
    }
    return {};
  }

  const CuttlefishConfig::InstanceSpecific instance_;
  LogTeeCreator& log_tee_;
  DeviceSockets rotary_sockets_;
  DeviceSockets mouse_sockets_;
  DeviceSockets gamepad_sockets_;
  DeviceSockets keyboard_sockets_;
  DeviceSockets switches_sockets_;
  std::vector<DeviceSockets> touchscreen_sockets_;
  std::vector<DeviceSockets> touchpad_sockets_;
};

}  // namespace
fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 InputPathsProvider, LogTeeCreator>
VhostInputDevicesComponent() {
  return fruit::createComponent()
      .bind<InputPathsProvider, VhostInputDevices>()
      .addMultibinding<CommandSource, VhostInputDevices>()
      .addMultibinding<SetupFeature, VhostInputDevices>();
}

}  // namespace cuttlefish
