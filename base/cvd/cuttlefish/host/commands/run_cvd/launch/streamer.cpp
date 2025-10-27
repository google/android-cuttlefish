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

#include "cuttlefish/host/commands/run_cvd/launch/streamer.h"

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <fruit/component.h>
#include <fruit/fruit_forward_decls.h>
#include <fruit/macro.h>

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/posix/strerror.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/run_cvd/launch/enable_multitouch.h"
#include "cuttlefish/host/commands/run_cvd/launch/input_connections_provider.h"
#include "cuttlefish/host/commands/run_cvd/launch/sensors_socket_pair.h"
#include "cuttlefish/host/commands/run_cvd/launch/webrtc_controller.h"
#include "cuttlefish/host/commands/run_cvd/reporting.h"
#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/custom_actions.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/known_paths.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"
#include "cuttlefish/host/libs/feature/command_source.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/host/libs/feature/kernel_log_pipe_provider.h"

namespace cuttlefish {

namespace {

SharedFD CreateUnixInputServer(const std::string& path) {
  auto server =
      SharedFD::SocketLocalServer(path.c_str(), false, SOCK_STREAM, 0666);
  if (!server->IsOpen()) {
    LOG(ERROR) << "Unable to create unix input server: " << server->StrError();
    return {};
  }
  return server;
}

std::vector<Command> LaunchCustomActionServers(
    Command& webrtc_cmd,
    const std::vector<CustomActionServerConfig>& custom_actions) {
  bool first = true;
  std::vector<Command> commands;
  for (const auto& custom_action : custom_actions) {
    // Create a socket pair that will be used for communication between
    // WebRTC and the action server.
    SharedFD webrtc_socket, action_server_socket;
    if (!SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &webrtc_socket,
                              &action_server_socket)) {
      LOG(ERROR) << "Unable to create custom action server socket pair: "
                 << StrError(errno);
      continue;
    }

    // Launch the action server, providing its socket pair fd as the only
    // argument.
    auto binary = HostBinaryPath(custom_action.server);
    Command command(binary);
    command.AddParameter(action_server_socket);
    commands.emplace_back(std::move(command));

    // Pass the WebRTC socket pair fd to WebRTC.
    if (first) {
      first = false;
      webrtc_cmd.AddParameter("-action_servers=", custom_action.server, ":",
                              webrtc_socket);
    } else {
      webrtc_cmd.AppendToLastParameter(",", custom_action.server, ":",
                                       webrtc_socket);
    }
  }
  return commands;
}

// Creates the frame and input sockets and add the relevant arguments to
// webrtc commands
class StreamerSockets : public virtual SetupFeature {
 public:
  INJECT(StreamerSockets(const CuttlefishConfig& config,
                         InputConnectionsProvider& input_connections_provider,
                         const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config),
        instance_(instance),
        input_connections_provider_(input_connections_provider) {}

  void AppendCommandArguments(Command& cmd) {
    const int touch_count = instance_.display_configs().size() +
                            instance_.touchpad_configs().size();
    if (touch_count > 0) {
      cmd.AddParameter("--multitouch=", ShouldEnableMultitouch(instance_));
      std::vector<SharedFD> touch_connections =
          input_connections_provider_.TouchscreenConnections();
      for (const SharedFD& touchpad_connection :
           input_connections_provider_.TouchpadConnections()) {
        touch_connections.push_back(touchpad_connection);
      }
      cmd.AddParameter("-touch_fds=", touch_connections[0]);
      for (int i = 1; i < touch_connections.size(); ++i) {
        cmd.AppendToLastParameter(",", touch_connections[i]);
      }
    }
    if (instance_.enable_mouse()) {
      cmd.AddParameter("-mouse_fd=",
                       input_connections_provider_.MouseConnection());
    }
    if (instance_.enable_gamepad()) {
      cmd.AddParameter("-gamepad_fd=",
                       input_connections_provider_.GamepadConnection());
    }
    cmd.AddParameter("-rotary_fd=",
                     input_connections_provider_.RotaryDeviceConnection());
    cmd.AddParameter("-keyboard_fd=",
                     input_connections_provider_.KeyboardConnection());
    cmd.AddParameter("-frame_server_fd=", frames_server_);
    if (instance_.enable_audio()) {
      cmd.AddParameter("--audio_server_fd=", audio_server_);
    }
    cmd.AddParameter("--confui_in_fd=", confui_in_fd_);
    cmd.AddParameter("--confui_out_fd=", confui_out_fd_);
    cmd.AddParameter("-switches_fd=",
                     input_connections_provider_.SwitchesConnection());
  }

  // SetupFeature
  std::string Name() const override { return "StreamerSockets"; }
  bool Enabled() const override {
    bool is_qemu = config_.vm_manager() == VmmMode::kQemu;
    bool is_accelerated = instance_.gpu_mode() != kGpuModeGuestSwiftshader;
    return !(is_qemu && is_accelerated);
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {&input_connections_provider_};
  }

  Result<void> ResultSetup() override {
    frames_server_ = CreateUnixInputServer(instance_.frames_socket_path());
    CF_EXPECT(frames_server_->IsOpen(), frames_server_->StrError());
    // TODO(schuffelen): Make this a separate optional feature?
    if (instance_.enable_audio()) {
      auto path = config_.ForDefaultInstance().audio_server_path();
      audio_server_ =
          SharedFD::SocketLocalServer(path, false, SOCK_SEQPACKET, 0666);
      CF_EXPECT(audio_server_->IsOpen(), audio_server_->StrError());
    }
    CF_EXPECT(InitializeVConsoles());
    return {};
  }

  Result<void> InitializeVConsoles() {
    std::vector<std::string> fifo_files = {
        instance_.PerInstanceInternalPath("confui_fifo_vm.in"),
        instance_.PerInstanceInternalPath("confui_fifo_vm.out"),
    };
    for (const auto& path : fifo_files) {
      unlink(path.c_str());
    }
    std::vector<SharedFD> fds;
    for (const auto& path : fifo_files) {
      fds.emplace_back(CF_EXPECT(SharedFD::Fifo(path, 0660)));
    }
    confui_in_fd_ = fds[0];
    confui_out_fd_ = fds[1];
    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  InputConnectionsProvider& input_connections_provider_;
  SharedFD frames_server_;
  SharedFD audio_server_;
  SharedFD confui_in_fd_;   // host -> guest
  SharedFD confui_out_fd_;  // guest -> host
};

class WebRtcServer : public virtual CommandSource,
                     public DiagnosticInformation,
                     public KernelLogPipeConsumer {
 public:
  INJECT(WebRtcServer(const CuttlefishConfig& config,
                      const CuttlefishConfig::InstanceSpecific& instance,
                      StreamerSockets& sockets,
                      KernelLogPipeProvider& log_pipe_provider,
                      const CustomActionConfigProvider& custom_action_config,
                      WebRtcController& webrtc_controller,
                      AutoSensorsSocketPair::Type& sensors_socket_pair))
      : config_(config),
        instance_(instance),
        sockets_(sockets),
        log_pipe_provider_(log_pipe_provider),
        custom_action_config_(custom_action_config),
        webrtc_controller_(webrtc_controller),
        sensors_socket_pair_(sensors_socket_pair) {}
  // DiagnosticInformation
  std::vector<std::string> Diagnostics() const override {
    if (!Enabled()) {
      return {};
    }
    std::ostringstream out;
    out << "Point your browser to https://localhost:"
        << config_.sig_server_proxy_port() << " to interact with the device.";
    return {out.str()};
  }

  // CommandSource
  Result<std::vector<MonitorCommand>> Commands() override {
    std::vector<MonitorCommand> commands;

    // Start a TCP proxy to make the host signaling server available on the
    // legacy port.
    Command sig_proxy(WebRtcSigServerProxyBinary());
    sig_proxy.AddParameter("-server_port=", config_.sig_server_proxy_port());
    commands.emplace_back(std::move(sig_proxy));

    auto stopper = [webrtc_controller = webrtc_controller_]() mutable {
      (void)webrtc_controller.SendStopRecordingCommand();
      return StopperResult::kStopFailure;
    };

    Command webrtc(WebRtcBinary(), KillSubprocessFallback(stopper));

    webrtc.UnsetFromEnvironment("http_proxy");
    sockets_.AppendCommandArguments(webrtc);
    webrtc.AddParameter("--command_fd=", webrtc_controller_.GetClientSocket());
    webrtc.AddParameter("-kernel_log_events_fd=", kernel_log_events_pipe_);
    webrtc.AddParameter("-client_dir=",
                        DefaultHostArtifactsPath("usr/share/webrtc/assets"));

    // TODO get from launcher params
    const auto& actions =
        custom_action_config_.CustomActionServers(instance_.id());
    for (auto& action : LaunchCustomActionServers(webrtc, actions)) {
      commands.emplace_back(std::move(action));
    }

    webrtc.AddParameter("-sensors_fd=",
                        sensors_socket_pair_->sensors_simulator_socket);

    commands.emplace_back(std::move(webrtc));
    return commands;
  }

  // SetupFeature
  bool Enabled() const override {
    if (!sockets_.Enabled()) {
      return false;
    }
    switch (config_.vm_manager()) {
      case VmmMode::kCrosvm:
      case VmmMode::kQemu:
        return true;
      case VmmMode::kGem5:
      case VmmMode::kUnknown:
        return false;
    }
  }

 private:
  std::string Name() const override { return "WebRtcServer"; }
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {static_cast<SetupFeature*>(&sockets_),
            static_cast<SetupFeature*>(&log_pipe_provider_),
            static_cast<SetupFeature*>(&webrtc_controller_),
            static_cast<SetupFeature*>(&sensors_socket_pair_)};
  }

  Result<void> ResultSetup() override {
    kernel_log_events_pipe_ = log_pipe_provider_.KernelLogPipe();
    CF_EXPECT(kernel_log_events_pipe_->IsOpen(),
              kernel_log_events_pipe_->StrError());
    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  StreamerSockets& sockets_;
  KernelLogPipeProvider& log_pipe_provider_;
  const CustomActionConfigProvider& custom_action_config_;
  WebRtcController& webrtc_controller_;
  SharedFD kernel_log_events_pipe_;
  SharedFD switches_server_;
  AutoSensorsSocketPair::Type& sensors_socket_pair_;
};

}  // namespace

fruit::Component<fruit::Required<
    const CuttlefishConfig, KernelLogPipeProvider, InputConnectionsProvider,
    const CuttlefishConfig::InstanceSpecific, const CustomActionConfigProvider,
    WebRtcController>>
launchStreamerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, WebRtcServer>()
      .addMultibinding<DiagnosticInformation, WebRtcServer>()
      .addMultibinding<KernelLogPipeConsumer, WebRtcServer>()
      .addMultibinding<SetupFeature, StreamerSockets>()
      .addMultibinding<SetupFeature, WebRtcServer>();
}

}  // namespace cuttlefish

