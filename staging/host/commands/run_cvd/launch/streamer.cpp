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
#include <sstream>
#include <string>
#include <utility>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/commands/run_cvd/reporting.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

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
        << strerror(errno);
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
                         const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  void AppendCommandArguments(Command& cmd) {
    if (config_.vm_manager() == vm_manager::QemuManager::name()) {
      cmd.AddParameter("-write_virtio_input");
    }
    if (!touch_servers_.empty()) {
      cmd.AddParameter("-touch_fds=", touch_servers_[0]);
      for (int i = 1; i < touch_servers_.size(); ++i) {
        cmd.AppendToLastParameter(",", touch_servers_[i]);
      }
    }
    cmd.AddParameter("-keyboard_fd=", keyboard_server_);
    cmd.AddParameter("-frame_server_fd=", frames_server_);
    if (config_.enable_audio()) {
      cmd.AddParameter("--audio_server_fd=", audio_server_);
    }
    cmd.AddParameter("--confui_in_fd=", confui_in_fd_);
    cmd.AddParameter("--confui_out_fd=", confui_out_fd_);
  }

  // SetupFeature
  std::string Name() const override { return "StreamerSockets"; }
  bool Enabled() const override {
    bool is_qemu = config_.vm_manager() == vm_manager::QemuManager::name();
    bool is_accelerated = instance_.gpu_mode() != kGpuModeGuestSwiftshader;
    return !(is_qemu && is_accelerated);
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }

  Result<void> ResultSetup() override {
    auto use_vsockets = config_.vm_manager() == vm_manager::QemuManager::name();
    for (int i = 0; i < instance_.display_configs().size(); ++i) {
      SharedFD touch_socket =
          use_vsockets ? SharedFD::VsockServer(instance_.touch_server_port(),
                                               SOCK_STREAM)
                       : CreateUnixInputServer(instance_.touch_socket_path(i));
      CF_EXPECT(touch_socket->IsOpen(), touch_socket->StrError());
      touch_servers_.emplace_back(std::move(touch_socket));
    }
    keyboard_server_ =
        use_vsockets ? SharedFD::VsockServer(instance_.keyboard_server_port(),
                                             SOCK_STREAM)
                     : CreateUnixInputServer(instance_.keyboard_socket_path());
    CF_EXPECT(keyboard_server_->IsOpen(), keyboard_server_->StrError());

    frames_server_ = CreateUnixInputServer(instance_.frames_socket_path());
    CF_EXPECT(frames_server_->IsOpen(), frames_server_->StrError());
    // TODO(schuffelen): Make this a separate optional feature?
    if (config_.enable_audio()) {
      auto path = config_.ForDefaultInstance().audio_server_path();
      audio_server_ =
          SharedFD::SocketLocalServer(path, false, SOCK_SEQPACKET, 0666);
      CF_EXPECT(audio_server_->IsOpen(), audio_server_->StrError());
    }
    AddConfUiFifo();
    return {};
  }

  Result<void> AddConfUiFifo() {
    std::vector<std::string> fifo_files = {
        instance_.PerInstanceInternalPath("confui_fifo_vm.in"),
        instance_.PerInstanceInternalPath("confui_fifo_vm.out")};
    for (const auto& path : fifo_files) {
      unlink(path.c_str());
    }
    std::vector<SharedFD> fds;
    for (const auto& path : fifo_files) {
      CF_EXPECT(mkfifo(path.c_str(), 0660) == 0, "Could not create " << path);
      auto fd = SharedFD::Open(path, O_RDWR);
      CF_EXPECT(fd->IsOpen(),
                "Could not open " << path << ": " << fd->StrError());
      fds.emplace_back(fd);
    }
    confui_in_fd_ = fds[0];
    confui_out_fd_ = fds[1];
    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  std::vector<SharedFD> touch_servers_;
  SharedFD keyboard_server_;
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
                      const CustomActionConfigProvider& custom_action_config))
      : config_(config),
        instance_(instance),
        sockets_(sockets),
        log_pipe_provider_(log_pipe_provider),
        custom_action_config_(custom_action_config) {}
  // DiagnosticInformation
  std::vector<std::string> Diagnostics() const override {
    if (!Enabled() ||
        !(config_.ForDefaultInstance().start_webrtc_sig_server() ||
          config_.ForDefaultInstance().start_webrtc_sig_server_proxy())) {
      // When WebRTC is enabled but an operator other than the one launched by
      // run_cvd is used there is no way to know the url to which to point the
      // browser to.
      return {};
    }
    std::ostringstream out;
    out << "Point your browser to https://localhost:"
        << config_.sig_server_port() << " to interact with the device.";
    return {out.str()};
  }

  // CommandSource
  Result<std::vector<Command>> Commands() override {
    std::vector<Command> commands;
    if (instance_.start_webrtc_sig_server()) {
      Command sig_server(WebRtcSigServerBinary());
      sig_server.AddParameter("-assets_dir=", config_.webrtc_assets_dir());
      sig_server.AddParameter("-use_secure_http=",
                              config_.sig_server_secure() ? "true" : "false");
      if (!config_.webrtc_certs_dir().empty()) {
        sig_server.AddParameter("-certs_dir=", config_.webrtc_certs_dir());
      }
      sig_server.AddParameter("-http_server_port=", config_.sig_server_port());
      commands.emplace_back(std::move(sig_server));
    }

    if (instance_.start_webrtc_sig_server_proxy()) {
      Command sig_proxy(WebRtcSigServerProxyBinary());
      sig_proxy.AddParameter("-server_port=", config_.sig_server_port());
      commands.emplace_back(std::move(sig_proxy));
    }

    auto stopper = [host_socket = std::move(host_socket_)](Subprocess* proc) {
      struct timeval timeout;
      timeout.tv_sec = 3;
      timeout.tv_usec = 0;
      CHECK(host_socket->SetSockOpt(SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                    sizeof(timeout)) == 0)
          << "Could not set receive timeout";

      WriteAll(host_socket, "C");
      char response[1];
      int read_ret = host_socket->Read(response, sizeof(response));
      if (read_ret != 0) {
        LOG(ERROR) << "Failed to read response from webrtc";
        return KillSubprocess(proc);
      }
      return KillSubprocess(proc) == StopperResult::kStopSuccess
                 ? StopperResult::kStopCrash
                 : StopperResult::kStopFailure;
    };

    Command webrtc(WebRtcBinary(), stopper);
    webrtc.UnsetFromEnvironment("http_proxy");
    sockets_.AppendCommandArguments(webrtc);
    if (config_.vm_manager() == vm_manager::CrosvmManager::name()) {
      webrtc.AddParameter("-switches_fd=", switches_server_);
    }
    // Currently there is no way to ensure the signaling server will already
    // have bound the socket to the port by the time the webrtc process runs
    // (the common technique of doing it from the launcher is not possible here
    // as the server library being used creates its own sockets). However, this
    // issue is mitigated slightly by doing some retrying and backoff in the
    // webrtc process when connecting to the websocket, so it shouldn't be an
    // issue most of the time.
    webrtc.AddParameter("--command_fd=", client_socket_);
    webrtc.AddParameter("-kernel_log_events_fd=", kernel_log_events_pipe_);
    webrtc.AddParameter("-client_dir=",
                        DefaultHostArtifactsPath("usr/share/webrtc/assets"));

    // TODO get from launcher params
    const auto& actions = custom_action_config_.CustomActionServers();
    for (auto& action : LaunchCustomActionServers(webrtc, actions)) {
      commands.emplace_back(std::move(action));
    }
    commands.emplace_back(std::move(webrtc));

    return commands;
  }

  // SetupFeature
  bool Enabled() const override {
    return sockets_.Enabled() && config_.enable_webrtc();
  }

 private:
  std::string Name() const override { return "WebRtcServer"; }
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {static_cast<SetupFeature*>(&sockets_),
            static_cast<SetupFeature*>(&log_pipe_provider_)};
  }

  Result<void> ResultSetup() override {
    CF_EXPECT(SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &client_socket_,
                                   &host_socket_),
              client_socket_->StrError());
    if (config_.vm_manager() == vm_manager::CrosvmManager::name()) {
      switches_server_ =
          CreateUnixInputServer(instance_.switches_socket_path());
      CF_EXPECT(switches_server_->IsOpen(), switches_server_->StrError());
    }
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
  SharedFD kernel_log_events_pipe_;
  SharedFD client_socket_;
  SharedFD host_socket_;
  SharedFD switches_server_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig, KernelLogPipeProvider,
                                 const CuttlefishConfig::InstanceSpecific,
                                 const CustomActionConfigProvider>>
launchStreamerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, WebRtcServer>()
      .addMultibinding<DiagnosticInformation, WebRtcServer>()
      .addMultibinding<KernelLogPipeConsumer, WebRtcServer>()
      .addMultibinding<SetupFeature, StreamerSockets>()
      .addMultibinding<SetupFeature, WebRtcServer>();
}

}  // namespace cuttlefish
