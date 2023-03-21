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

#include "host/frontend/webrtc/libdevice/streamer.h"

#include <android-base/logging.h>
#include <json/json.h>

#include <api/audio_codecs/audio_decoder_factory.h>
#include <api/audio_codecs/audio_encoder_factory.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/peer_connection_interface.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_encoder_factory.h>
#include <media/base/video_broadcaster.h>
#include <pc/video_track_source.h>

#include "host/frontend/webrtc/libcommon/audio_device.h"
#include "host/frontend/webrtc/libcommon/peer_connection_utils.h"
#include "host/frontend/webrtc/libcommon/port_range_socket_factory.h"
#include "host/frontend/webrtc/libcommon/utils.h"
#include "host/frontend/webrtc/libcommon/vp8only_encoder_factory.h"
#include "host/frontend/webrtc/libdevice/audio_track_source_impl.h"
#include "host/frontend/webrtc/libdevice/camera_streamer.h"
#include "host/frontend/webrtc/libdevice/client_handler.h"
#include "host/frontend/webrtc/libdevice/video_track_source_impl.h"
#include "host/frontend/webrtc_operator/constants/signaling_constants.h"

namespace cuttlefish {
namespace webrtc_streaming {
namespace {

constexpr auto kStreamIdField = "stream_id";
constexpr auto kXResField = "x_res";
constexpr auto kYResField = "y_res";
constexpr auto kDpiField = "dpi";
constexpr auto kIsTouchField = "is_touch";
constexpr auto kDisplaysField = "displays";
constexpr auto kAudioStreamsField = "audio_streams";
constexpr auto kHardwareField = "hardware";
constexpr auto kControlPanelButtonCommand = "command";
constexpr auto kControlPanelButtonTitle = "title";
constexpr auto kControlPanelButtonIconName = "icon_name";
constexpr auto kControlPanelButtonShellCommand = "shell_command";
constexpr auto kControlPanelButtonDeviceStates = "device_states";
constexpr auto kControlPanelButtonLidSwitchOpen = "lid_switch_open";
constexpr auto kControlPanelButtonHingeAngleValue = "hinge_angle_value";
constexpr auto kCustomControlPanelButtonsField = "custom_control_panel_buttons";

constexpr int kRegistrationRetries = 3;
constexpr int kRetryFirstIntervalMs = 1000;
constexpr int kReconnectRetries = 100;
constexpr int kReconnectIntervalMs = 1000;

bool ParseMessage(const uint8_t* data, size_t length, Json::Value* msg_out) {
  auto str = reinterpret_cast<const char*>(data);
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> json_reader(builder.newCharReader());
  std::string errorMessage;
  return json_reader->parse(str, str + length, msg_out, &errorMessage);
}

struct DisplayDescriptor {
  int width;
  int height;
  int dpi;
  bool touch_enabled;
  rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> source;
};

struct ControlPanelButtonDescriptor {
  std::string command;
  std::string title;
  std::string icon_name;
  std::optional<std::string> shell_command;
  std::vector<DeviceState> device_states;
};

// TODO (jemoreira): move to a place in common with the signaling server
struct OperatorServerConfig {
  std::vector<webrtc::PeerConnectionInterface::IceServer> servers;
};

// Wraps a scoped_refptr pointer to an audio device module
class AudioDeviceModuleWrapper : public AudioSource {
 public:
  AudioDeviceModuleWrapper(
      rtc::scoped_refptr<CfAudioDeviceModule> device_module)
      : device_module_(device_module) {}
  int GetMoreAudioData(void* data, int bytes_per_sample,
                       int samples_per_channel, int num_channels,
                       int sample_rate, bool& muted) override {
    return device_module_->GetMoreAudioData(data, bytes_per_sample,
                                            samples_per_channel, num_channels,
                                            sample_rate, muted);
  }

  rtc::scoped_refptr<CfAudioDeviceModule> device_module() {
    return device_module_;
  }

 private:
  rtc::scoped_refptr<CfAudioDeviceModule> device_module_;
};

}  // namespace


class Streamer::Impl : public ServerConnectionObserver,
                       public PeerConnectionBuilder,
                       public std::enable_shared_from_this<ServerConnectionObserver> {
 public:
  std::shared_ptr<ClientHandler> CreateClientHandler(int client_id);

  void Register(std::weak_ptr<OperatorObserver> observer);

  void SendMessageToClient(int client_id, const Json::Value& msg);
  void DestroyClientHandler(int client_id);
  void SetupCameraForClient(int client_id);

  // WsObserver
  void OnOpen() override;
  void OnClose() override;
  void OnError(const std::string& error) override;
  void OnReceive(const uint8_t* msg, size_t length, bool is_binary) override;

  void HandleConfigMessage(const Json::Value& msg);
  void HandleClientMessage(const Json::Value& server_message);

  // PeerConnectionBuilder
  Result<rtc::scoped_refptr<webrtc::PeerConnectionInterface>> Build(
      webrtc::PeerConnectionObserver& observer,
      const std::vector<webrtc::PeerConnectionInterface::IceServer>&
          per_connection_servers) override;

  // All accesses to these variables happen from the signal_thread, so there is
  // no need for extra synchronization mechanisms (mutex)
  StreamerConfig config_;
  OperatorServerConfig operator_config_;
  std::unique_ptr<ServerConnection> server_connection_;
  std::shared_ptr<ConnectionObserverFactory> connection_observer_factory_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;
  std::unique_ptr<rtc::Thread> network_thread_;
  std::unique_ptr<rtc::Thread> worker_thread_;
  std::unique_ptr<rtc::Thread> signal_thread_;
  std::map<std::string, DisplayDescriptor> displays_;
  std::map<std::string, rtc::scoped_refptr<AudioTrackSourceImpl>>
      audio_sources_;
  std::map<int, std::shared_ptr<ClientHandler>> clients_;
  std::weak_ptr<OperatorObserver> operator_observer_;
  std::map<std::string, std::string> hardware_;
  std::vector<ControlPanelButtonDescriptor> custom_control_panel_buttons_;
  std::shared_ptr<AudioDeviceModuleWrapper> audio_device_module_;
  std::unique_ptr<CameraStreamer> camera_streamer_;
  int registration_retries_left_ = kRegistrationRetries;
  int retry_interval_ms_ = kRetryFirstIntervalMs;
  LocalRecorder* recorder_ = nullptr;
};

Streamer::Streamer(std::unique_ptr<Streamer::Impl> impl)
    : impl_(std::move(impl)) {}

/* static */
std::unique_ptr<Streamer> Streamer::Create(
    const StreamerConfig& cfg, LocalRecorder* recorder,
    std::shared_ptr<ConnectionObserverFactory> connection_observer_factory) {
  rtc::LogMessage::LogToDebug(rtc::LS_ERROR);

  std::unique_ptr<Streamer::Impl> impl(new Streamer::Impl());
  impl->config_ = cfg;
  impl->recorder_ = recorder;
  impl->connection_observer_factory_ = connection_observer_factory;

  auto network_thread_result = CreateAndStartThread("network-thread");
  if (!network_thread_result.ok()) {
    LOG(ERROR) << network_thread_result.error().Trace();
    return nullptr;
  }
  impl->network_thread_ = std::move(*network_thread_result);

  auto worker_thread_result = CreateAndStartThread("worker-thread");
  if (!worker_thread_result.ok()) {
    LOG(ERROR) << worker_thread_result.error().Trace();
    return nullptr;
  }
  impl->worker_thread_ = std::move(*worker_thread_result);

  auto signal_thread_result = CreateAndStartThread("signal-thread");
  if (!signal_thread_result.ok()) {
    LOG(ERROR) << signal_thread_result.error().Trace();
    return nullptr;
  }
  impl->signal_thread_ = std::move(*signal_thread_result);

  impl->audio_device_module_ = std::make_shared<AudioDeviceModuleWrapper>(
      rtc::scoped_refptr<CfAudioDeviceModule>(
          new rtc::RefCountedObject<CfAudioDeviceModule>()));

  auto result = CreatePeerConnectionFactory(
      impl->network_thread_.get(), impl->worker_thread_.get(),
      impl->signal_thread_.get(), impl->audio_device_module_->device_module());

  if (!result.ok()) {
    LOG(ERROR) << result.error().Trace();
    return nullptr;
  }
  impl->peer_connection_factory_ = *result;

  return std::unique_ptr<Streamer>(new Streamer(std::move(impl)));
}

std::shared_ptr<VideoSink> Streamer::AddDisplay(const std::string& label,
                                                int width, int height, int dpi,
                                                bool touch_enabled) {
  // Usually called from an application thread
  return impl_->signal_thread_->BlockingCall(
      [this, &label, width, height, dpi,
       touch_enabled]() -> std::shared_ptr<VideoSink> {
        if (impl_->displays_.count(label)) {
          LOG(ERROR) << "Display with same label already exists: " << label;
          return nullptr;
        }
        rtc::scoped_refptr<VideoTrackSourceImpl> source(
            new rtc::RefCountedObject<VideoTrackSourceImpl>(width, height));
        impl_->displays_[label] = {width, height, dpi, touch_enabled, source};

        auto video_track = impl_->peer_connection_factory_->CreateVideoTrack(
            label, source.get());

        for (auto& [_, client] : impl_->clients_) {
          client->AddDisplay(video_track, label);
        }

        if (impl_->recorder_) {
          rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> source2 =
              source;
          auto deleter = [](webrtc::VideoTrackSourceInterface* source) {
            source->Release();
          };
          std::shared_ptr<webrtc::VideoTrackSourceInterface> source_shared(
              source2.release(), deleter);
          impl_->recorder_->AddDisplay(width, height, source_shared);
        }

        return std::shared_ptr<VideoSink>(
            new VideoTrackSourceImplSinkWrapper(source));
      });
}

bool Streamer::RemoveDisplay(const std::string& label) {
  // Usually called from an application thread
  return impl_->signal_thread_->BlockingCall(
      [this, &label]() -> bool {
        for (auto& [_, client] : impl_->clients_) {
          client->RemoveDisplay(label);
        }

        impl_->displays_.erase(label);
        return true;
      });
}

std::shared_ptr<AudioSink> Streamer::AddAudioStream(const std::string& label) {
  // Usually called from an application thread
  return impl_->signal_thread_->BlockingCall(
      [this, &label]() -> std::shared_ptr<AudioSink> {
        if (impl_->audio_sources_.count(label)) {
          LOG(ERROR) << "Audio stream with same label already exists: "
                     << label;
          return nullptr;
        }
        rtc::scoped_refptr<AudioTrackSourceImpl> source(
            new rtc::RefCountedObject<AudioTrackSourceImpl>());
        impl_->audio_sources_[label] = source;
        return std::shared_ptr<AudioSink>(
            new AudioTrackSourceImplSinkWrapper(source));
      });
}

std::shared_ptr<AudioSource> Streamer::GetAudioSource() {
  return impl_->audio_device_module_;
}

CameraController* Streamer::AddCamera(unsigned int port, unsigned int cid) {
  impl_->camera_streamer_ = std::make_unique<CameraStreamer>(port, cid);
  return impl_->camera_streamer_.get();
}

void Streamer::SetHardwareSpec(std::string key, std::string value) {
  impl_->hardware_.emplace(key, value);
}

void Streamer::AddCustomControlPanelButton(const std::string& command,
                                           const std::string& title,
                                           const std::string& icon_name) {
  ControlPanelButtonDescriptor button = {
      .command = command, .title = title, .icon_name = icon_name};
  impl_->custom_control_panel_buttons_.push_back(button);
}

void Streamer::AddCustomControlPanelButtonWithShellCommand(
    const std::string& command, const std::string& title,
    const std::string& icon_name, const std::string& shell_command) {
  ControlPanelButtonDescriptor button = {
      .command = command, .title = title, .icon_name = icon_name};
  button.shell_command = shell_command;
  impl_->custom_control_panel_buttons_.push_back(button);
}

void Streamer::AddCustomControlPanelButtonWithDeviceStates(
    const std::string& command, const std::string& title,
    const std::string& icon_name,
    const std::vector<DeviceState>& device_states) {
  ControlPanelButtonDescriptor button = {
      .command = command, .title = title, .icon_name = icon_name};
  button.device_states = device_states;
  impl_->custom_control_panel_buttons_.push_back(button);
}

void Streamer::Register(std::weak_ptr<OperatorObserver> observer) {
  // Usually called from an application thread
  // No need to block the calling thread on this, the observer will be notified
  // when the connection is established.
  impl_->signal_thread_->PostTask([this, observer]() {
    impl_->Register(observer);
  });
}

void Streamer::Unregister() {
  // Usually called from an application thread.
  impl_->signal_thread_->PostTask(
      [this]() { impl_->server_connection_.reset(); });
}

void Streamer::Impl::Register(std::weak_ptr<OperatorObserver> observer) {
  operator_observer_ = observer;
  // When the connection is established the OnOpen function will be called where
  // the registration will take place
  if (!server_connection_) {
    server_connection_ =
        ServerConnection::Connect(config_.operator_server, weak_from_this());
  } else {
    // in case connection attempt is retried, just call Reconnect().
    // Recreating server_connection_ object will destroy existing WSConnection
    // object and task re-scheduling will fail
    server_connection_->Reconnect();
  }
}

void Streamer::Impl::OnOpen() {
  // Called from the websocket thread.
  // Connected to operator.
  signal_thread_->PostTask([this]() {
    Json::Value register_obj;
    register_obj[cuttlefish::webrtc_signaling::kTypeField] =
        cuttlefish::webrtc_signaling::kRegisterType;
    register_obj[cuttlefish::webrtc_signaling::kDeviceIdField] =
        config_.device_id;
    CHECK(config_.client_files_port >= 0) << "Invalid device port provided";
    register_obj[cuttlefish::webrtc_signaling::kDevicePortField] =
        config_.client_files_port;

    Json::Value device_info;
    Json::Value displays(Json::ValueType::arrayValue);
    // No need to synchronize with other accesses to display_ because all
    // happens on signal_thread.
    for (auto& entry : displays_) {
      Json::Value display;
      display[kStreamIdField] = entry.first;
      display[kXResField] = entry.second.width;
      display[kYResField] = entry.second.height;
      display[kDpiField] = entry.second.dpi;
      display[kIsTouchField] = true;
      displays.append(display);
    }
    device_info[kDisplaysField] = displays;
    Json::Value audio_streams(Json::ValueType::arrayValue);
    for (auto& entry : audio_sources_) {
      Json::Value audio;
      audio[kStreamIdField] = entry.first;
      audio_streams.append(audio);
    }
    device_info[kAudioStreamsField] = audio_streams;
    Json::Value hardware;
    for (const auto& [k, v] : hardware_) {
      hardware[k] = v;
    }
    device_info[kHardwareField] = hardware;
    Json::Value custom_control_panel_buttons(Json::arrayValue);
    for (const auto& button : custom_control_panel_buttons_) {
      Json::Value button_entry;
      button_entry[kControlPanelButtonCommand] = button.command;
      button_entry[kControlPanelButtonTitle] = button.title;
      button_entry[kControlPanelButtonIconName] = button.icon_name;
      if (button.shell_command) {
        button_entry[kControlPanelButtonShellCommand] = *(button.shell_command);
      } else if (!button.device_states.empty()) {
        Json::Value device_states(Json::arrayValue);
        for (const DeviceState& device_state : button.device_states) {
          Json::Value device_state_entry;
          if (device_state.lid_switch_open) {
            device_state_entry[kControlPanelButtonLidSwitchOpen] =
                *device_state.lid_switch_open;
          }
          if (device_state.hinge_angle_value) {
            device_state_entry[kControlPanelButtonHingeAngleValue] =
                *device_state.hinge_angle_value;
          }
          device_states.append(device_state_entry);
        }
        button_entry[kControlPanelButtonDeviceStates] = device_states;
      }
      custom_control_panel_buttons.append(button_entry);
    }
    device_info[kCustomControlPanelButtonsField] = custom_control_panel_buttons;
    register_obj[cuttlefish::webrtc_signaling::kDeviceInfoField] = device_info;
    server_connection_->Send(register_obj);
    // Do this last as OnRegistered() is user code and may take some time to
    // complete (although it shouldn't...)
    auto observer = operator_observer_.lock();
    if (observer) {
      observer->OnRegistered();
    }
  });
}

void Streamer::Impl::OnClose() {
  // Called from websocket thread
  // The operator shouldn't close the connection with the client, it's up to the
  // device to decide when to disconnect.
  LOG(WARNING) << "Connection with server closed unexpectedly";
  signal_thread_->PostTask([this]() {
    auto observer = operator_observer_.lock();
    if (observer) {
      observer->OnClose();
    }
  });
  LOG(INFO) << "Trying to re-connect to operator..";
  registration_retries_left_ = kReconnectRetries;
  retry_interval_ms_ = kReconnectIntervalMs;
  signal_thread_->PostDelayedTask(
      [this]() { Register(operator_observer_); },
      webrtc::TimeDelta::Millis(retry_interval_ms_));
}

void Streamer::Impl::OnError(const std::string& error) {
  // Called from websocket thread.
  if (registration_retries_left_) {
    LOG(WARNING) << "Connection to operator failed (" << error << "), "
                 << registration_retries_left_ << " retries left"
                 << " (will retry in " << retry_interval_ms_ / 1000 << "s)";
    --registration_retries_left_;
    signal_thread_->PostDelayedTask(
        [this]() {
          // Need to reconnect and register again with operator
          Register(operator_observer_);
        },
        webrtc::TimeDelta::Millis(retry_interval_ms_));
    retry_interval_ms_ *= 2;
  } else {
    LOG(ERROR) << "Error on connection with the operator: " << error;
    signal_thread_->PostTask([this]() {
      auto observer = operator_observer_.lock();
      if (observer) {
        observer->OnError();
      }
    });
  }
}

void Streamer::Impl::HandleConfigMessage(const Json::Value& server_message) {
  CHECK(signal_thread_->IsCurrent())
      << __FUNCTION__ << " called from the wrong thread";
  auto result = ParseIceServersMessage(server_message);
  if (!result.ok()) {
    LOG(WARNING) << "Failed to parse ice servers message from server: "
                 << result.error().Trace();
  }
  operator_config_.servers = *result;
}

void Streamer::Impl::HandleClientMessage(const Json::Value& server_message) {
  CHECK(signal_thread_->IsCurrent())
      << __FUNCTION__ << " called from the wrong thread";
  if (!server_message.isMember(cuttlefish::webrtc_signaling::kClientIdField) ||
      !server_message[cuttlefish::webrtc_signaling::kClientIdField].isInt()) {
    LOG(ERROR) << "Client message received without valid client id";
    return;
  }
  auto client_id =
      server_message[cuttlefish::webrtc_signaling::kClientIdField].asInt();
  if (!server_message.isMember(cuttlefish::webrtc_signaling::kPayloadField)) {
    LOG(WARNING) << "Received empty client message";
    return;
  }
  auto client_message =
      server_message[cuttlefish::webrtc_signaling::kPayloadField];
  if (clients_.count(client_id) == 0) {
    auto client_handler = CreateClientHandler(client_id);
    if (!client_handler) {
      LOG(ERROR) << "Failed to create a new client handler";
      return;
    }
    clients_.emplace(client_id, client_handler);
  }
  auto client_handler = clients_[client_id];

  client_handler->HandleMessage(client_message);
}

void Streamer::Impl::OnReceive(const uint8_t* msg, size_t length,
                               bool is_binary) {
  // Usually called from websocket thread.
  Json::Value server_message;
  // Once OnReceive returns the buffer can be destroyed/recycled at any time, so
  // parse the data into a JSON object while still on the websocket thread.
  if (is_binary || !ParseMessage(msg, length, &server_message)) {
    LOG(ERROR) << "Received invalid JSON from server: '"
               << (is_binary ? std::string("(binary_data)")
                             : std::string(msg, msg + length))
               << "'";
    return;
  }
  // Transition to the signal thread before member variables are accessed.
  signal_thread_->PostTask([this, server_message]() {
    if (!server_message.isMember(cuttlefish::webrtc_signaling::kTypeField) ||
        !server_message[cuttlefish::webrtc_signaling::kTypeField].isString()) {
      LOG(ERROR) << "No message_type field from server";
      // Notify the caller
      OnError(
          "Invalid message received from operator: no message type field "
          "present");
      return;
    }
    auto type =
        server_message[cuttlefish::webrtc_signaling::kTypeField].asString();
    if (type == cuttlefish::webrtc_signaling::kConfigType) {
      HandleConfigMessage(server_message);
    } else if (type == cuttlefish::webrtc_signaling::kClientDisconnectType) {
      if (!server_message.isMember(
              cuttlefish::webrtc_signaling::kClientIdField) ||
          !server_message.isMember(
              cuttlefish::webrtc_signaling::kClientIdField)) {
        LOG(ERROR) << "Invalid disconnect message received from server";
        // Notify the caller
        OnError("Invalid disconnect message: client_id is required");
        return;
      }
      auto client_id =
          server_message[cuttlefish::webrtc_signaling::kClientIdField].asInt();
      LOG(INFO) << "Client " << client_id << " has disconnected.";
      DestroyClientHandler(client_id);
    } else if (type == cuttlefish::webrtc_signaling::kClientMessageType) {
      HandleClientMessage(server_message);
    } else {
      LOG(ERROR) << "Unknown message type: " << type;
      // Notify the caller
      OnError("Invalid message received from operator: unknown message type");
      return;
    }
  });
}

std::shared_ptr<ClientHandler> Streamer::Impl::CreateClientHandler(
    int client_id) {
  CHECK(signal_thread_->IsCurrent())
      << __FUNCTION__ << " called from the wrong thread";
  auto observer = connection_observer_factory_->CreateObserver();

  auto client_handler = ClientHandler::Create(
      client_id, observer, *this,
      [this, client_id](const Json::Value& msg) {
        SendMessageToClient(client_id, msg);
      },
      [this, client_id](bool isOpen) {
        if (isOpen) {
          SetupCameraForClient(client_id);
        } else {
          DestroyClientHandler(client_id);
        }
      });

  for (auto& entry : displays_) {
    auto& label = entry.first;
    auto& video_source = entry.second.source;

    auto video_track =
        peer_connection_factory_->CreateVideoTrack(label, video_source.get());
    client_handler->AddDisplay(video_track, label);
  }

  for (auto& entry : audio_sources_) {
    auto& label = entry.first;
    auto& audio_stream = entry.second;
    auto audio_track =
        peer_connection_factory_->CreateAudioTrack(label, audio_stream.get());
    client_handler->AddAudio(audio_track, label);
  }

  return client_handler;
}

Result<rtc::scoped_refptr<webrtc::PeerConnectionInterface>>
Streamer::Impl::Build(
    webrtc::PeerConnectionObserver& observer,
    const std::vector<webrtc::PeerConnectionInterface::IceServer>&
        per_connection_servers) {
  webrtc::PeerConnectionDependencies dependencies(&observer);
  auto servers = operator_config_.servers;
  servers.insert(servers.end(), per_connection_servers.begin(),
                 per_connection_servers.end());
  if (config_.udp_port_range != config_.tcp_port_range) {
    // libwebrtc removed the ability to provide a packet socket factory when
    // creating a peer connection. They plan to provide that functionality with
    // the peer connection factory, but that's currently incomplete (the packet
    // socket factory is ignored by the peer connection factory). The only other
    // choice to customize port ranges is through the port allocator config, but
    // this is suboptimal as it only allows to specify a single port range that
    // will be use for both tcp and udp ports.
    LOG(WARNING) << "TCP and UDP port ranges differ, TCP connections may not "
                    "work properly";
  }
  return CF_EXPECT(
      CreatePeerConnection(peer_connection_factory_, std::move(dependencies),
                           config_.udp_port_range.first,
                           config_.udp_port_range.second, servers),
      "Failed to build peer connection");
}

void Streamer::Impl::SendMessageToClient(int client_id,
                                         const Json::Value& msg) {
  LOG(VERBOSE) << "Sending to client: " << msg.toStyledString();
  CHECK(signal_thread_->IsCurrent())
      << __FUNCTION__ << " called from the wrong thread";
  Json::Value wrapper;
  wrapper[cuttlefish::webrtc_signaling::kPayloadField] = msg;
  wrapper[cuttlefish::webrtc_signaling::kTypeField] =
      cuttlefish::webrtc_signaling::kForwardType;
  wrapper[cuttlefish::webrtc_signaling::kClientIdField] = client_id;
  // This is safe to call from the webrtc threads because
  // ServerConnection(s) are thread safe
  server_connection_->Send(wrapper);
}

void Streamer::Impl::DestroyClientHandler(int client_id) {
  // Usually called from signal thread, could be called from websocket thread or
  // an application thread.
  signal_thread_->PostTask([this, client_id]() {
    // This needs to be 'posted' to the thread instead of 'invoked'
    // immediately for two reasons:
    // * The client handler is destroyed by this code, it's generally a
    // bad idea (though not necessarily wrong) to return to a member
    // function of a destroyed object.
    // * The client handler may call this from within a peer connection
    // observer callback, destroying the client handler there leads to a
    // deadlock.
    clients_.erase(client_id);
  });
}

void Streamer::Impl::SetupCameraForClient(int client_id) {
  if (!camera_streamer_) {
    return;
  }
  auto client_handler = clients_[client_id];
  if (client_handler) {
    auto camera_track = client_handler->GetCameraStream();
    if (camera_track) {
      camera_track->AddOrUpdateSink(camera_streamer_.get(),
                                    rtc::VideoSinkWants());
    }
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
