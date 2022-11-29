/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/input.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <libyuv.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "host/frontend/webrtc/audio_handler.h"
#include "host/frontend/webrtc/client_server.h"
#include "host/frontend/webrtc/connection_observer.h"
#include "host/frontend/webrtc/display_handler.h"
#include "host/frontend/webrtc/kernel_log_events_handler.h"
#include "host/frontend/webrtc/libdevice/camera_controller.h"
#include "host/frontend/webrtc/libdevice/local_recorder.h"
#include "host/frontend/webrtc/libdevice/streamer.h"
#include "host/frontend/webrtc/libdevice/video_sink.h"
#include "host/libs/audio_connector/server.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"
#include "host/libs/confui/host_mode_ctrl.h"
#include "host/libs/confui/host_server.h"
#include "host/libs/screen_connector/screen_connector.h"

DEFINE_string(touch_fds, "",
              "A list of fds to listen on for touch connections.");
DEFINE_int32(keyboard_fd, -1, "An fd to listen on for keyboard connections.");
DEFINE_int32(switches_fd, -1, "An fd to listen on for switch connections.");
DEFINE_int32(frame_server_fd, -1, "An fd to listen on for frame updates");
DEFINE_int32(kernel_log_events_fd, -1,
             "An fd to listen on for kernel log events.");
DEFINE_int32(command_fd, -1, "An fd to listen to for control messages");
DEFINE_int32(confui_in_fd, -1,
             "Confirmation UI virtio-console from host to guest");
DEFINE_int32(confui_out_fd, -1,
             "Confirmation UI virtio-console from guest to host");
DEFINE_string(action_servers, "",
              "A comma-separated list of server_name:fd pairs, "
              "where each entry corresponds to one custom action server.");
DEFINE_bool(write_virtio_input, true,
            "Whether to send input events in virtio format.");
DEFINE_int32(audio_server_fd, -1, "An fd to listen on for audio frames");
DEFINE_int32(camera_streamer_fd, -1, "An fd to send client camera frames");
DEFINE_string(client_dir, "webrtc", "Location of the client files");

using cuttlefish::AudioHandler;
using cuttlefish::CfConnectionObserverFactory;
using cuttlefish::DisplayHandler;
using cuttlefish::KernelLogEventsHandler;
using cuttlefish::webrtc_streaming::LocalRecorder;
using cuttlefish::webrtc_streaming::Streamer;
using cuttlefish::webrtc_streaming::StreamerConfig;
using cuttlefish::webrtc_streaming::VideoSink;
using cuttlefish::webrtc_streaming::ServerConfig;

class CfOperatorObserver
    : public cuttlefish::webrtc_streaming::OperatorObserver {
 public:
  virtual ~CfOperatorObserver() = default;
  virtual void OnRegistered() override {
    LOG(VERBOSE) << "Registered with Operator";
  }
  virtual void OnClose() override {
    LOG(ERROR) << "Connection with Operator unexpectedly closed";
  }
  virtual void OnError() override {
    LOG(ERROR) << "Error encountered in connection with Operator";
  }
};

static std::vector<std::pair<std::string, std::string>> ParseHttpHeaders(
    const std::string& path) {
  auto fd = cuttlefish::SharedFD::Open(path, O_RDONLY);
  if (!fd->IsOpen()) {
    LOG(WARNING) << "Unable to open operator (signaling server) headers file, "
                    "connecting to the operator will probably fail: "
                 << fd->StrError();
    return {};
  }
  std::string raw_headers;
  auto res = cuttlefish::ReadAll(fd, &raw_headers);
  if (res < 0) {
    LOG(WARNING) << "Unable to open operator (signaling server) headers file, "
                    "connecting to the operator will probably fail: "
                 << fd->StrError();
    return {};
  }
  std::vector<std::pair<std::string, std::string>> headers;
  std::size_t raw_index = 0;
  while (raw_index < raw_headers.size()) {
    auto colon_pos = raw_headers.find(':', raw_index);
    if (colon_pos == std::string::npos) {
      LOG(ERROR)
          << "Expected to find ':' in each line of the operator headers file";
      break;
    }
    auto eol_pos = raw_headers.find('\n', colon_pos);
    if (eol_pos == std::string::npos) {
      eol_pos = raw_headers.size();
    }
    // If the file uses \r\n as line delimiters exclude the \r too.
    auto eov_pos = raw_headers[eol_pos - 1] == '\r'? eol_pos - 1: eol_pos;
    headers.emplace_back(
        raw_headers.substr(raw_index, colon_pos + 1 - raw_index),
        raw_headers.substr(colon_pos + 1, eov_pos - colon_pos - 1));
    raw_index = eol_pos + 1;
  }
  return headers;
}

std::unique_ptr<cuttlefish::AudioServer> CreateAudioServer() {
  cuttlefish::SharedFD audio_server_fd =
      cuttlefish::SharedFD::Dup(FLAGS_audio_server_fd);
  close(FLAGS_audio_server_fd);
  return std::make_unique<cuttlefish::AudioServer>(audio_server_fd);
}

fruit::Component<cuttlefish::CustomActionConfigProvider> WebRtcComponent() {
  return fruit::createComponent()
      .install(cuttlefish::ConfigFlagPlaceholder)
      .install(cuttlefish::CustomActionsComponent);
};

int main(int argc, char** argv) {
  cuttlefish::DefaultSubprocessLogging(argv);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);

  cuttlefish::InputSockets input_sockets;

  auto counter = 0;
  for (const auto& touch_fd_str : android::base::Split(FLAGS_touch_fds, ",")) {
    auto touch_fd = std::stoi(touch_fd_str);
    input_sockets.touch_servers["display_" + std::to_string(counter++)] =
        cuttlefish::SharedFD::Dup(touch_fd);
    close(touch_fd);
  }
  input_sockets.keyboard_server = cuttlefish::SharedFD::Dup(FLAGS_keyboard_fd);
  input_sockets.switches_server = cuttlefish::SharedFD::Dup(FLAGS_switches_fd);
  auto control_socket = cuttlefish::SharedFD::Dup(FLAGS_command_fd);
  close(FLAGS_keyboard_fd);
  close(FLAGS_switches_fd);
  close(FLAGS_command_fd);
  // Accepting on these sockets here means the device won't register with the
  // operator as soon as it could, but rather wait until crosvm's input display
  // devices have been initialized. That's OK though, because without those
  // devices there is no meaningful interaction the user can have with the
  // device.
  for (const auto& touch_entry : input_sockets.touch_servers) {
    input_sockets.touch_clients[touch_entry.first] =
        cuttlefish::SharedFD::Accept(*touch_entry.second);
  }
  input_sockets.keyboard_client =
      cuttlefish::SharedFD::Accept(*input_sockets.keyboard_server);
  input_sockets.switches_client =
      cuttlefish::SharedFD::Accept(*input_sockets.switches_server);

  std::vector<std::thread> touch_accepters;
  for (const auto& touch : input_sockets.touch_servers) {
    auto label = touch.first;
    touch_accepters.emplace_back([label, &input_sockets]() {
      for (;;) {
        input_sockets.touch_clients[label] =
            cuttlefish::SharedFD::Accept(*input_sockets.touch_servers[label]);
      }
    });
  }
  std::thread keyboard_accepter([&input_sockets]() {
    for (;;) {
      input_sockets.keyboard_client =
          cuttlefish::SharedFD::Accept(*input_sockets.keyboard_server);
    }
  });
  std::thread switches_accepter([&input_sockets]() {
    for (;;) {
      input_sockets.switches_client =
          cuttlefish::SharedFD::Accept(*input_sockets.switches_server);
    }
  });

  auto kernel_log_events_client =
      cuttlefish::SharedFD::Dup(FLAGS_kernel_log_events_fd);
  close(FLAGS_kernel_log_events_fd);

  auto cvd_config = cuttlefish::CuttlefishConfig::Get();
  auto instance = cvd_config->ForDefaultInstance();
  auto& host_mode_ctrl = cuttlefish::HostModeCtrl::Get();
  auto screen_connector_ptr = cuttlefish::DisplayHandler::ScreenConnector::Get(
      FLAGS_frame_server_fd, host_mode_ctrl);
  auto& screen_connector = *(screen_connector_ptr.get());
  auto client_server = cuttlefish::ClientFilesServer::New(FLAGS_client_dir);
  CHECK(client_server) << "Failed to initialize client files server";

  // create confirmation UI service, giving host_mode_ctrl and
  // screen_connector
  // keep this singleton object alive until the webRTC process ends
  auto confui_to_guest_fd = cuttlefish::SharedFD::Dup(FLAGS_confui_in_fd);
  close(FLAGS_confui_in_fd);
  auto confui_from_guest_fd = cuttlefish::SharedFD::Dup(FLAGS_confui_out_fd);
  close(FLAGS_confui_out_fd);

  auto& host_confui_server = cuttlefish::confui::HostServer::Get(
      host_mode_ctrl, screen_connector, confui_from_guest_fd,
      confui_to_guest_fd);

  StreamerConfig streamer_config;

  streamer_config.device_id = instance.webrtc_device_id();
  streamer_config.client_files_port = client_server->port();
  streamer_config.tcp_port_range = cvd_config->webrtc_tcp_port_range();
  streamer_config.udp_port_range = cvd_config->webrtc_udp_port_range();
  streamer_config.operator_server.addr = cvd_config->sig_server_address();
  streamer_config.operator_server.port = cvd_config->sig_server_port();
  streamer_config.operator_server.path = cvd_config->sig_server_path();
  if (cvd_config->sig_server_secure()) {
    streamer_config.operator_server.security =
        cvd_config->sig_server_strict()
            ? ServerConfig::Security::kStrict
            : ServerConfig::Security::kAllowSelfSigned;
  } else {
    streamer_config.operator_server.security =
        ServerConfig::Security::kInsecure;
  }

  if (!cvd_config->sig_server_headers_path().empty()) {
    streamer_config.operator_server.http_headers =
        ParseHttpHeaders(cvd_config->sig_server_headers_path());
  }

  KernelLogEventsHandler kernel_logs_event_handler(kernel_log_events_client);
  auto observer_factory = std::make_shared<CfConnectionObserverFactory>(
      input_sockets, &kernel_logs_event_handler, host_confui_server);

  auto streamer = Streamer::Create(streamer_config, observer_factory);
  CHECK(streamer) << "Could not create streamer";

  auto display_handler =
      std::make_shared<DisplayHandler>(*streamer, screen_connector);

  if (instance.camera_server_port()) {
    auto camera_controller = streamer->AddCamera(instance.camera_server_port(),
                                                 instance.vsock_guest_cid());
    observer_factory->SetCameraHandler(camera_controller);
  }

  std::unique_ptr<cuttlefish::webrtc_streaming::LocalRecorder> local_recorder;
  if (cvd_config->record_screen()) {
    int recording_num = 0;
    std::string recording_path;
    do {
      recording_path = instance.PerInstancePath("recording/recording_");
      recording_path += std::to_string(recording_num);
      recording_path += ".webm";
      recording_num++;
    } while (cuttlefish::FileExists(recording_path));
    local_recorder = LocalRecorder::Create(recording_path);
    CHECK(local_recorder) << "Could not create local recorder";

    streamer->RecordDisplays(*local_recorder);
  }

  observer_factory->SetDisplayHandler(display_handler);

  streamer->SetHardwareSpec("CPUs", instance.cpus());
  streamer->SetHardwareSpec("RAM", std::to_string(instance.memory_mb()) + " mb");

  std::string user_friendly_gpu_mode;
  if (instance.gpu_mode() == cuttlefish::kGpuModeGuestSwiftshader) {
    user_friendly_gpu_mode = "SwiftShader (Guest CPU Rendering)";
  } else if (instance.gpu_mode() == cuttlefish::kGpuModeDrmVirgl) {
    user_friendly_gpu_mode = "VirglRenderer (Accelerated Host GPU Rendering)";
  } else if (instance.gpu_mode() == cuttlefish::kGpuModeGfxStream) {
    user_friendly_gpu_mode = "Gfxstream (Accelerated Host GPU Rendering)";
  } else {
    user_friendly_gpu_mode = instance.gpu_mode();
  }
  streamer->SetHardwareSpec("GPU Mode", user_friendly_gpu_mode);

  std::shared_ptr<AudioHandler> audio_handler;
  if (instance.enable_audio()) {
    auto audio_stream = streamer->AddAudioStream("audio");
    auto audio_server = CreateAudioServer();
    auto audio_source = streamer->GetAudioSource();
    audio_handler = std::make_shared<AudioHandler>(std::move(audio_server),
                                                   audio_stream, audio_source);
  }

  // Parse the -action_servers flag, storing a map of action server name -> fd
  std::map<std::string, int> action_server_fds;
  for (const std::string& action_server :
       android::base::Split(FLAGS_action_servers, ",")) {
    if (action_server.empty()) {
      continue;
    }
    const std::vector<std::string> server_and_fd =
        android::base::Split(action_server, ":");
    CHECK(server_and_fd.size() == 2)
        << "Wrong format for action server flag: " << action_server;
    std::string server = server_and_fd[0];
    int fd = std::stoi(server_and_fd[1]);
    action_server_fds[server] = fd;
  }

  fruit::Injector<cuttlefish::CustomActionConfigProvider> injector(
      WebRtcComponent);
  for (auto& fragment :
       injector.getMultibindings<cuttlefish::ConfigFragment>()) {
    CHECK(cvd_config->LoadFragment(*fragment))
        << "Failed to load config fragment";
  }

  const auto& actions_provider =
      injector.get<cuttlefish::CustomActionConfigProvider&>();

  for (const auto& custom_action : actions_provider.CustomShellActions()) {
    const auto button = custom_action.button;
    streamer->AddCustomControlPanelButtonWithShellCommand(
        button.command, button.title, button.icon_name,
        custom_action.shell_command);
  }

  for (const auto& custom_action : actions_provider.CustomActionServers()) {
    if (action_server_fds.find(custom_action.server) ==
        action_server_fds.end()) {
      LOG(ERROR) << "Custom action server not provided as command line flag: "
                 << custom_action.server;
      continue;
    }
    LOG(INFO) << "Connecting to custom action server " << custom_action.server;

    int fd = action_server_fds[custom_action.server];
    cuttlefish::SharedFD custom_action_server = cuttlefish::SharedFD::Dup(fd);
    close(fd);

    if (custom_action_server->IsOpen()) {
      std::vector<std::string> commands_for_this_server;
      for (const auto& button : custom_action.buttons) {
        streamer->AddCustomControlPanelButton(button.command, button.title,
                                              button.icon_name);
        commands_for_this_server.push_back(button.command);
      }
      observer_factory->AddCustomActionServer(custom_action_server,
                                              commands_for_this_server);
    } else {
      LOG(ERROR) << "Error connecting to custom action server: "
                 << custom_action.server;
    }
  }

  for (const auto& custom_action : actions_provider.CustomDeviceStateActions()) {
      const auto button = custom_action.button;
      streamer->AddCustomControlPanelButtonWithDeviceStates(
          button.command, button.title, button.icon_name,
          custom_action.device_states);
  }

  std::shared_ptr<cuttlefish::webrtc_streaming::OperatorObserver> operator_observer(
      new CfOperatorObserver());
  streamer->Register(operator_observer);

  std::thread control_thread([control_socket, &local_recorder]() {
    if (!local_recorder) {
      return;
    }
    std::string message = "_";
    int read_ret;
    while ((read_ret = cuttlefish::ReadExact(control_socket, &message)) > 0) {
      LOG(VERBOSE) << "received control message: " << message;
      if (message[0] == 'C') {
        LOG(DEBUG) << "Finalizing screen recording...";
        local_recorder->Stop();
        LOG(INFO) << "Finalized screen recording.";
        message = "Y";
        cuttlefish::WriteAll(control_socket, message);
      }
    }
    LOG(DEBUG) << "control socket closed";
  });

  if (audio_handler) {
    audio_handler->Start();
  }
  host_confui_server.Start();
  display_handler->Loop();

  return 0;
}
