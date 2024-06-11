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

#include <memory>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>
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
#include "host/frontend/webrtc/libdevice/lights_observer.h"
#include "host/frontend/webrtc/libdevice/local_recorder.h"
#include "host/frontend/webrtc/libdevice/streamer.h"
#include "host/frontend/webrtc/libdevice/video_sink.h"
#include "host/libs/audio_connector/server.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/logging.h"
#include "host/libs/config/openwrt_args.h"
#include "host/libs/input_connector/socket_input_connector.h"

DEFINE_bool(multitouch, true,
            "Whether to send multi-touch or single-touch events");
DEFINE_string(touch_fds, "",
              "A list of fds to listen on for touch connections.");
DEFINE_int32(rotary_fd, -1, "An fd to listen on for rotary connections.");
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
DEFINE_int32(sensors_in_fd, -1, "Sensors virtio-console from host to guest");
DEFINE_int32(sensors_out_fd, -1, "Sensors virtio-console from guest to host");
DEFINE_string(action_servers, "",
              "A comma-separated list of server_name:fd pairs, "
              "where each entry corresponds to one custom action server.");
DEFINE_bool(write_virtio_input, true,
            "Whether to send input events in virtio format.");
DEFINE_int32(audio_server_fd, -1, "An fd to listen on for audio frames");
DEFINE_int32(camera_streamer_fd, -1, "An fd to send client camera frames");
DEFINE_string(client_dir, "webrtc", "Location of the client files");
DEFINE_string(group_id, "", "The group id of device");

using cuttlefish::AudioHandler;
using cuttlefish::CfConnectionObserverFactory;
using cuttlefish::DisplayHandler;
using cuttlefish::KernelLogEventsHandler;
using cuttlefish::webrtc_streaming::RecordingManager;
using cuttlefish::webrtc_streaming::ServerConfig;
using cuttlefish::webrtc_streaming::Streamer;
using cuttlefish::webrtc_streaming::StreamerConfig;
using cuttlefish::webrtc_streaming::VideoSink;

constexpr auto kOpewnrtWanIpAddressName = "wan_ipaddr";
constexpr auto kTouchscreenPrefix = "display_";
constexpr auto kTouchpadPrefix = "touch_";

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

  auto control_socket = cuttlefish::SharedFD::Dup(FLAGS_command_fd);
  close(FLAGS_command_fd);

  auto cvd_config = cuttlefish::CuttlefishConfig::Get();
  auto instance = cvd_config->ForDefaultInstance();

  cuttlefish::InputSocketsConnectorBuilder inputs_builder(
      FLAGS_write_virtio_input ? cuttlefish::InputEventType::Virtio
                               : cuttlefish::InputEventType::Evdev);

  const auto display_count = instance.display_configs().size();
  const auto touch_fds = android::base::Split(FLAGS_touch_fds, ",");
  CHECK(touch_fds.size() == display_count + instance.touchpad_configs().size())
      << "Number of touch FDs does not match the number of configured displays "
         "and touchpads";
  for (int i = 0; i < touch_fds.size(); i++) {
    int touch_fd;
    CHECK(android::base::ParseInt(touch_fds[i], &touch_fd))
        << "Invalid touch_fd: " << touch_fds[i];
    // Displays are listed first, then touchpads
    auto label_prefix =
        i < display_count ? kTouchscreenPrefix : kTouchpadPrefix;
    auto device_idx = i < display_count ? i : i - display_count;
    auto device_label = fmt::format("{}{}", label_prefix, device_idx);
    auto touch_shared_fd = cuttlefish::SharedFD::Dup(touch_fd);
    if (FLAGS_multitouch) {
      inputs_builder.WithMultitouchDevice(device_label, touch_shared_fd);
    } else {
      inputs_builder.WithTouchDevice(device_label, touch_shared_fd);
    }
    close(touch_fd);
  }
  if (FLAGS_rotary_fd >= 0) {
    inputs_builder.WithRotary(cuttlefish::SharedFD::Dup(FLAGS_rotary_fd));
    close(FLAGS_rotary_fd);
  }
  if (FLAGS_keyboard_fd >= 0) {
    inputs_builder.WithKeyboard(cuttlefish::SharedFD::Dup(FLAGS_keyboard_fd));
    close(FLAGS_keyboard_fd);
  }
  if (FLAGS_switches_fd >= 0) {
    inputs_builder.WithSwitches(cuttlefish::SharedFD::Dup(FLAGS_switches_fd));
    close(FLAGS_switches_fd);
  }

  auto input_connector = std::move(inputs_builder).Build();

  auto kernel_log_events_client =
      cuttlefish::SharedFD::Dup(FLAGS_kernel_log_events_fd);
  close(FLAGS_kernel_log_events_fd);

  auto client_server = cuttlefish::ClientFilesServer::New(FLAGS_client_dir);
  CHECK(client_server) << "Failed to initialize client files server";

  StreamerConfig streamer_config;

  streamer_config.device_id = instance.webrtc_device_id();
  streamer_config.group_id = FLAGS_group_id;
  streamer_config.client_files_port = client_server->port();
  streamer_config.tcp_port_range = instance.webrtc_tcp_port_range();
  streamer_config.udp_port_range = instance.webrtc_udp_port_range();
  streamer_config.openwrt_device_id =
      cvd_config->Instances()[0].webrtc_device_id();
  streamer_config.openwrt_addr = OpenwrtArgsFromConfig(
      cvd_config->Instances()[0])[kOpewnrtWanIpAddressName];
  streamer_config.control_env_proxy_server_path =
      instance.grpc_socket_path() + "/ControlEnvProxyServer.sock";
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

  KernelLogEventsHandler kernel_logs_event_handler(kernel_log_events_client);

  std::shared_ptr<cuttlefish::webrtc_streaming::LightsObserver> lights_observer;
  if (instance.lights_server_port()) {
    lights_observer =
        std::make_shared<cuttlefish::webrtc_streaming::LightsObserver>(
            instance.lights_server_port(), instance.vsock_guest_cid(),
            instance.vhost_user_vsock());
    lights_observer->Start();
  }

  auto observer_factory = std::make_shared<CfConnectionObserverFactory>(
      *input_connector.get(), &kernel_logs_event_handler, lights_observer);

  RecordingManager recording_manager;

  auto streamer =
      Streamer::Create(streamer_config, recording_manager, observer_factory);
  CHECK(streamer) << "Could not create streamer";

  int frames_fd = FLAGS_frame_server_fd;
  bool frames_are_rgba = !instance.guest_uses_bgra_framebuffers();
  auto display_handler =
      std::make_shared<DisplayHandler>(*streamer, frames_fd, frames_are_rgba);

  if (instance.camera_server_port()) {
    auto camera_controller = streamer->AddCamera(instance.camera_server_port(),
                                                 instance.vsock_guest_cid(),
                                                 instance.vhost_user_vsock());
    observer_factory->SetCameraHandler(camera_controller);
    streamer->SetHardwareSpec("camera_passthrough", true);
  }

  observer_factory->SetDisplayHandler(display_handler);

  const auto touchpad_configs = instance.touchpad_configs();
  for (int i = 0; i < touchpad_configs.size(); i++) {
    streamer->AddTouchpad(kTouchpadPrefix + std::to_string(i),
                          touchpad_configs[i].width,
                          touchpad_configs[i].height);
  }

  streamer->SetHardwareSpec("CPUs", instance.cpus());
  streamer->SetHardwareSpec("RAM", std::to_string(instance.memory_mb()) + " mb");

  std::string user_friendly_gpu_mode;
  if (instance.gpu_mode() == cuttlefish::kGpuModeGuestSwiftshader) {
    user_friendly_gpu_mode = "SwiftShader (Guest CPU Rendering)";
  } else if (instance.gpu_mode() == cuttlefish::kGpuModeDrmVirgl) {
    user_friendly_gpu_mode =
        "VirglRenderer (Accelerated Rendering using Host OpenGL)";
  } else if (instance.gpu_mode() == cuttlefish::kGpuModeGfxstream) {
    user_friendly_gpu_mode =
        "Gfxstream (Accelerated Rendering using Host OpenGL and Vulkan)";
  } else if (instance.gpu_mode() == cuttlefish::kGpuModeGfxstreamGuestAngle) {
    user_friendly_gpu_mode =
        "Gfxstream (Accelerated Rendering using Host Vulkan)";
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

  for (const auto& custom_action :
       actions_provider.CustomShellActions(instance.id())) {
    const auto button = custom_action.button;
    streamer->AddCustomControlPanelButtonWithShellCommand(
        button.command, button.title, button.icon_name,
        custom_action.shell_command);
  }

  for (const auto& custom_action :
       actions_provider.CustomActionServers(instance.id())) {
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

  for (const auto& custom_action :
       actions_provider.CustomDeviceStateActions(instance.id())) {
    const auto button = custom_action.button;
    streamer->AddCustomControlPanelButtonWithDeviceStates(
        button.command, button.title, button.icon_name,
        custom_action.device_states);
  }

  std::shared_ptr<cuttlefish::webrtc_streaming::OperatorObserver> operator_observer(
      new CfOperatorObserver());
  streamer->Register(operator_observer);

  std::thread control_thread([control_socket, &recording_manager]() {
    std::string message = "_";
    int read_ret;
    while ((read_ret = cuttlefish::ReadExact(control_socket, &message)) > 0) {
      LOG(VERBOSE) << "received control message: " << message;
      if (message[0] == 'T') {
        LOG(INFO) << "Received command to start recording in main.cpp.";
        recording_manager.Start();
      } else if (message[0] == 'C') {
        LOG(INFO) << "Received command to stop recording in main.cpp.";
        recording_manager.Stop();
      }
      // Send feedback an indication of command received.
      CHECK(cuttlefish::WriteAll(control_socket, "Y") == 1) << "Failed to send response: "
                                                            << control_socket->StrError();
    }
    LOG(DEBUG) << "control socket closed";
  });

  if (audio_handler) {
    audio_handler->Start();
  }

  if (instance.record_screen()) {
    LOG(VERBOSE) << "Waiting for recording manager initializing.";
    recording_manager.WaitForSources(instance.display_configs().size());
    recording_manager.Start();
  }

  control_thread.join();

  return 0;
}
