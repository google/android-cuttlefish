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
#include "host/commands/run_cvd/process_monitor.h"
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

// Creates the frame and input sockets and add the relevant arguments to the vnc
// server and webrtc commands
void CreateStreamerServers(Command* cmd, const CuttlefishConfig& config) {
  SharedFD touch_server;
  SharedFD keyboard_server;

  auto instance = config.ForDefaultInstance();
  if (config.vm_manager() == vm_manager::QemuManager::name()) {
    cmd->AddParameter("-write_virtio_input");

    touch_server =
        SharedFD::VsockServer(instance.touch_server_port(), SOCK_STREAM);
    keyboard_server =
        SharedFD::VsockServer(instance.keyboard_server_port(), SOCK_STREAM);
  } else {
    touch_server = CreateUnixInputServer(instance.touch_socket_path());
    keyboard_server = CreateUnixInputServer(instance.keyboard_socket_path());
  }
  if (!touch_server->IsOpen()) {
    LOG(ERROR) << "Could not open touch server: " << touch_server->StrError();
    return;
  }
  cmd->AddParameter("-touch_fd=", touch_server);

  if (!keyboard_server->IsOpen()) {
    LOG(ERROR) << "Could not open keyboard server: "
               << keyboard_server->StrError();
    return;
  }
  cmd->AddParameter("-keyboard_fd=", keyboard_server);

  if (config.enable_webrtc() &&
      config.vm_manager() == vm_manager::CrosvmManager::name()) {
    SharedFD switches_server =
        CreateUnixInputServer(instance.switches_socket_path());
    if (!switches_server->IsOpen()) {
      LOG(ERROR) << "Could not open switches server: "
                 << switches_server->StrError();
      return;
    }
    cmd->AddParameter("-switches_fd=", switches_server);
  }

  SharedFD frames_server = CreateUnixInputServer(instance.frames_socket_path());
  if (!frames_server->IsOpen()) {
    LOG(ERROR) << "Could not open frames server: " << frames_server->StrError();
    return;
  }
  cmd->AddParameter("-frame_server_fd=", frames_server);

  if (config.enable_audio()) {
    auto path = config.ForDefaultInstance().audio_server_path();
    auto audio_server =
        SharedFD::SocketLocalServer(path.c_str(), false, SOCK_SEQPACKET, 0666);
    if (!audio_server->IsOpen()) {
      LOG(ERROR) << "Could not create audio server: "
                 << audio_server->StrError();
      return;
    }
    cmd->AddParameter("--audio_server_fd=", audio_server);
  }
}

}  // namespace

void LaunchVNCServer(const CuttlefishConfig& config,
                     ProcessMonitor* process_monitor) {
  auto instance = config.ForDefaultInstance();
  // Launch the vnc server, don't wait for it to complete
  auto port_options = "-port=" + std::to_string(instance.vnc_server_port());
  Command vnc_server(VncServerBinary());
  vnc_server.AddParameter(port_options);

  CreateStreamerServers(&vnc_server, config);

  process_monitor->AddCommand(std::move(vnc_server));
}

void LaunchWebRTC(ProcessMonitor* process_monitor,
                  const CuttlefishConfig& config,
                  SharedFD kernel_log_events_pipe) {
  if (config.ForDefaultInstance().start_webrtc_sig_server()) {
    Command sig_server(WebRtcSigServerBinary());
    sig_server.AddParameter("-assets_dir=", config.webrtc_assets_dir());
    if (!config.webrtc_certs_dir().empty()) {
      sig_server.AddParameter("-certs_dir=", config.webrtc_certs_dir());
    }
    sig_server.AddParameter("-http_server_port=", config.sig_server_port());
    process_monitor->AddCommand(std::move(sig_server));
  }

  // Currently there is no way to ensure the signaling server will already have
  // bound the socket to the port by the time the webrtc process runs (the
  // common technique of doing it from the launcher is not possible here as the
  // server library being used creates its own sockets). However, this issue is
  // mitigated slightly by doing some retrying and backoff in the webrtc process
  // when connecting to the websocket, so it shouldn't be an issue most of the
  // time.
  SharedFD client_socket;
  SharedFD host_socket;
  CHECK(SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0, &client_socket,
                             &host_socket))
      << "Could not open command socket for webRTC";

  auto stopper = [host_socket = std::move(host_socket)](Subprocess* proc) {
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

  CreateStreamerServers(&webrtc, config);

  webrtc.AddParameter("--command_fd=", client_socket);

  webrtc.AddParameter("-kernel_log_events_fd=", kernel_log_events_pipe);

  LaunchCustomActionServers(webrtc, process_monitor, config);

  // TODO get from launcher params
  process_monitor->AddCommand(std::move(webrtc));
}

}  // namespace cuttlefish
