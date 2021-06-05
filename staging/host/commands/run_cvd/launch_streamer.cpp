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
#include <string>
#include <utility>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
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

std::vector<Command> LaunchCustomActionServers(Command& webrtc_cmd,
                                               const CuttlefishConfig& config) {
  bool first = true;
  std::vector<Command> commands;
  for (const auto& custom_action : config.custom_actions()) {
    if (custom_action.server) {
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
      std::string binary = "bin/" + *(custom_action.server);
      Command command(DefaultHostArtifactsPath(binary));
      command.AddParameter(action_server_socket);
      commands.emplace_back(std::move(command));

      // Pass the WebRTC socket pair fd to WebRTC.
      if (first) {
        first = false;
        webrtc_cmd.AddParameter("-action_servers=", *custom_action.server, ":",
                                webrtc_socket);
      } else {
        webrtc_cmd.AppendToLastParameter(",", *custom_action.server, ":",
                                         webrtc_socket);
      }
    }
  }
  return commands;
}

// Creates the frame and input sockets and add the relevant arguments to the vnc
// server and webrtc commands
class StreamerSockets : public virtual Feature {
 public:
  INJECT(StreamerSockets(const CuttlefishConfig& config,
                         const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  void AppendCommandArguments(Command& cmd) {
    if (config_.vm_manager() == vm_manager::QemuManager::name()) {
      cmd.AddParameter("-write_virtio_input");
    }
    cmd.AddParameter("-touch_fd=", touch_server_);
    cmd.AddParameter("-keyboard_fd=", keyboard_server_);
    cmd.AddParameter("-frame_server_fd=", frames_server_);
    if (config_.enable_audio()) {
      cmd.AddParameter("--audio_server_fd=", audio_server_);
    }
  }

  // Feature
  bool Enabled() const override { return true; }
  std::string Name() const override { return "StreamerSockets"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override {
    if (config_.vm_manager() == vm_manager::QemuManager::name()) {
      touch_server_ =
          SharedFD::VsockServer(instance_.touch_server_port(), SOCK_STREAM);
      keyboard_server_ =
          SharedFD::VsockServer(instance_.keyboard_server_port(), SOCK_STREAM);
    } else {
      touch_server_ = CreateUnixInputServer(instance_.touch_socket_path());
      keyboard_server_ =
          CreateUnixInputServer(instance_.keyboard_socket_path());
    }
    if (!touch_server_->IsOpen()) {
      LOG(ERROR) << "Failed to open touch server: "
                 << touch_server_->StrError();
      return false;
    }
    if (!keyboard_server_->IsOpen()) {
      LOG(ERROR) << "Failed to open keyboard server"
                 << keyboard_server_->StrError();
      return false;
    }
    frames_server_ = CreateUnixInputServer(instance_.frames_socket_path());
    if (!frames_server_->IsOpen()) {
      LOG(ERROR) << "Could not open frames server: "
                 << frames_server_->StrError();
      return false;
    }
    // TODO(schuffelen): Make this a separate optional feature?
    if (config_.enable_audio()) {
      auto path = config_.ForDefaultInstance().audio_server_path();
      audio_server_ =
          SharedFD::SocketLocalServer(path, false, SOCK_SEQPACKET, 0666);
      if (!audio_server_->IsOpen()) {
        LOG(ERROR) << "Could not create audio server: "
                   << audio_server_->StrError();
        return false;
      }
    }
    return true;
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD touch_server_;
  SharedFD keyboard_server_;
  SharedFD frames_server_;
  SharedFD audio_server_;
};

class VncServer : public virtual CommandSource {
 public:
  INJECT(VncServer(const CuttlefishConfig& config,
                   const CuttlefishConfig::InstanceSpecific& instance,
                   StreamerSockets& sockets))
      : config_(config), instance_(instance), sockets_(sockets) {}
  // CommandSource
  std::vector<Command> Commands() override {
    Command vnc_server(VncServerBinary());
    vnc_server.AddParameter("-port=", instance_.vnc_server_port());
    sockets_.AppendCommandArguments(vnc_server);

    std::vector<Command> commands;
    commands.emplace_back(std::move(vnc_server));
    return commands;
  }

  // Feature
  bool Enabled() const override { return config_.enable_vnc_server(); }
  std::string Name() const override { return "VncServer"; }
  std::unordered_set<Feature*> Dependencies() const override {
    return {static_cast<Feature*>(&sockets_)};
  }

 protected:
  bool Setup() override { return true; }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  StreamerSockets& sockets_;
};

class WebRtcServer : public virtual CommandSource {
 public:
  INJECT(WebRtcServer(const CuttlefishConfig& config,
                      const CuttlefishConfig::InstanceSpecific& instance,
                      StreamerSockets& sockets,
                      KernelLogPipeProvider& log_pipe_provider))
      : config_(config),
        instance_(instance),
        sockets_(sockets),
        log_pipe_provider_(log_pipe_provider) {}
  // CommandSource
  std::vector<Command> Commands() override {
    std::vector<Command> commands;
    if (instance_.start_webrtc_sig_server()) {
      Command sig_server(WebRtcSigServerBinary());
      sig_server.AddParameter("-assets_dir=", config_.webrtc_assets_dir());
      if (!config_.webrtc_certs_dir().empty()) {
        sig_server.AddParameter("-certs_dir=", config_.webrtc_certs_dir());
      }
      sig_server.AddParameter("-http_server_port=", config_.sig_server_port());
      commands.emplace_back(std::move(sig_server));
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
      }
      cuttlefish::KillSubprocess(proc);
      return true;
    };

    Command webrtc(WebRtcBinary(), SubprocessStopper(stopper));
    webrtc.UnsetFromEnvironment({"http_proxy"});
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

    // TODO get from launcher params
    for (auto& action : LaunchCustomActionServers(webrtc, config_)) {
      commands.emplace_back(std::move(action));
    }
    commands.emplace_back(std::move(webrtc));

    return commands;
  }

  // Feature
  bool Enabled() const override { return config_.enable_webrtc(); }
  std::string Name() const override { return "WebRtcServer"; }
  std::unordered_set<Feature*> Dependencies() const override {
    return {static_cast<Feature*>(&sockets_),
            static_cast<Feature*>(&log_pipe_provider_)};
  }

 protected:
  bool Setup() override {
    if (!SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &client_socket_,
                              &host_socket_)) {
      LOG(ERROR) << "Could not open command socket for webRTC";
      return false;
    }
    if (config_.vm_manager() == vm_manager::CrosvmManager::name()) {
      switches_server_ =
          CreateUnixInputServer(instance_.switches_socket_path());
      if (!switches_server_->IsOpen()) {
        LOG(ERROR) << "Could not open switches server: "
                   << switches_server_->StrError();
        return false;
      }
    }
    kernel_log_events_pipe_ = log_pipe_provider_.KernelLogPipe();
    if (!kernel_log_events_pipe_->IsOpen()) {
      LOG(ERROR) << "Failed to get a kernel log events pipe: "
                 << kernel_log_events_pipe_->StrError();
      return false;
    }
    return true;
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  StreamerSockets& sockets_;
  KernelLogPipeProvider& log_pipe_provider_;
  SharedFD kernel_log_events_pipe_;
  SharedFD client_socket_;
  SharedFD host_socket_;
  SharedFD switches_server_;
};

}  // namespace

fruit::Component<fruit::Required<const CuttlefishConfig, KernelLogPipeProvider,
                                 const CuttlefishConfig::InstanceSpecific>>
launchStreamerComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, WebRtcServer>()
      .addMultibinding<CommandSource, VncServer>()
      .addMultibinding<Feature, StreamerSockets>()
      .addMultibinding<Feature, WebRtcServer>()
      .addMultibinding<Feature, VncServer>();
}

}  // namespace cuttlefish
