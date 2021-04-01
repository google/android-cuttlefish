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

#include "host/frontend/webrtc/lib/streamer.h"

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

#include "host/frontend/webrtc/lib/audio_device.h"
#include "host/frontend/webrtc/lib/audio_track_source_impl.h"
#include "host/frontend/webrtc/lib/client_handler.h"
#include "host/frontend/webrtc/lib/port_range_socket_factory.h"
#include "host/frontend/webrtc/lib/video_track_source_impl.h"
#include "host/frontend/webrtc/lib/vp8only_encoder_factory.h"
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

void SendJson(WsConnection* ws_conn, const Json::Value& data) {
  Json::StreamWriterBuilder factory;
  auto data_str = Json::writeString(factory, data);
  ws_conn->Send(reinterpret_cast<const uint8_t*>(data_str.c_str()),
                data_str.size());
}

bool ParseMessage(const uint8_t* data, size_t length, Json::Value* msg_out) {
  auto str = reinterpret_cast<const char*>(data);
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> json_reader(builder.newCharReader());
  std::string errorMessage;
  return json_reader->parse(str, str + length, msg_out, &errorMessage);
}

std::unique_ptr<rtc::Thread> CreateAndStartThread(const std::string& name) {
  auto thread = rtc::Thread::CreateWithSocketServer();
  if (!thread) {
    LOG(ERROR) << "Failed to create " << name << " thread";
    return nullptr;
  }
  thread->SetName(name, nullptr);
  if (!thread->Start()) {
    LOG(ERROR) << "Failed to start " << name << " thread";
    return nullptr;
  }
  return thread;
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

class Streamer::Impl : public WsConnectionObserver {
 public:
  std::shared_ptr<ClientHandler> CreateClientHandler(int client_id);

  void SendMessageToClient(int client_id, const Json::Value& msg);
  void DestroyClientHandler(int client_id);

  // WsObserver
  void OnOpen() override;
  void OnClose() override;
  void OnError(const std::string& error) override;
  void OnReceive(const uint8_t* msg, size_t length, bool is_binary) override;

  void HandleConfigMessage(const Json::Value& msg);
  void HandleClientMessage(const Json::Value& server_message);

  // All accesses to these variables happen from the signal_thread, so there is
  // no need for extra synchronization mechanisms (mutex)
  StreamerConfig config_;
  OperatorServerConfig operator_config_;
  std::shared_ptr<WsConnection> server_connection_;
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
};

Streamer::Streamer(std::unique_ptr<Streamer::Impl> impl)
    : impl_(std::move(impl)) {}

/* static */
std::unique_ptr<Streamer> Streamer::Create(
    const StreamerConfig& cfg,
    std::shared_ptr<ConnectionObserverFactory> connection_observer_factory) {

  rtc::LogMessage::LogToDebug(rtc::LS_ERROR);

  std::unique_ptr<Streamer::Impl> impl(new Streamer::Impl());
  impl->config_ = cfg;
  impl->connection_observer_factory_ = connection_observer_factory;

  impl->network_thread_ = CreateAndStartThread("network-thread");
  impl->worker_thread_ = CreateAndStartThread("work-thread");
  impl->signal_thread_ = CreateAndStartThread("signal-thread");
  if (!impl->network_thread_ || !impl->worker_thread_ ||
      !impl->signal_thread_) {
    return nullptr;
  }

  impl->audio_device_module_ = std::make_shared<AudioDeviceModuleWrapper>(
      rtc::scoped_refptr<CfAudioDeviceModule>(
          new rtc::RefCountedObject<CfAudioDeviceModule>()));

  impl->peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
      impl->network_thread_.get(), impl->worker_thread_.get(),
      impl->signal_thread_.get(), impl->audio_device_module_->device_module(),
      webrtc::CreateBuiltinAudioEncoderFactory(),
      webrtc::CreateBuiltinAudioDecoderFactory(),
      std::make_unique<VP8OnlyEncoderFactory>(
          webrtc::CreateBuiltinVideoEncoderFactory()),
      webrtc::CreateBuiltinVideoDecoderFactory(), nullptr /* audio_mixer */,
      nullptr /* audio_processing */);

  if (!impl->peer_connection_factory_) {
    LOG(ERROR) << "Failed to create peer connection factory";
    return nullptr;
  }

  webrtc::PeerConnectionFactoryInterface::Options options;
  // By default the loopback network is ignored, but generating candidates for
  // it is useful when using TCP port forwarding.
  options.network_ignore_mask = 0;
  impl->peer_connection_factory_->SetOptions(options);

  return std::unique_ptr<Streamer>(new Streamer(std::move(impl)));
}

std::shared_ptr<VideoSink> Streamer::AddDisplay(const std::string& label,
                                                int width, int height, int dpi,
                                                bool touch_enabled) {
  // Usually called from an application thread
  return impl_->signal_thread_->Invoke<std::shared_ptr<VideoSink>>(
      RTC_FROM_HERE,
      [this, &label, width, height, dpi,
       touch_enabled]() -> std::shared_ptr<VideoSink> {
        if (impl_->displays_.count(label)) {
          LOG(ERROR) << "Display with same label already exists: " << label;
          return nullptr;
        }
        rtc::scoped_refptr<VideoTrackSourceImpl> source(
            new rtc::RefCountedObject<VideoTrackSourceImpl>(width, height));
        impl_->displays_[label] = {width, height, dpi, touch_enabled, source};
        return std::shared_ptr<VideoSink>(
            new VideoTrackSourceImplSinkWrapper(source));
      });
}

std::shared_ptr<AudioSink> Streamer::AddAudioStream(const std::string& label) {
  // Usually called from an application thread
  return impl_->signal_thread_->Invoke<std::shared_ptr<AudioSink>>(
      RTC_FROM_HERE, [this, &label]() -> std::shared_ptr<AudioSink> {
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
  impl_->signal_thread_->PostTask(RTC_FROM_HERE, [this, observer]() {
    impl_->operator_observer_ = observer;
    // This can be a local variable since the connection object will keep a
    // reference to it.
    auto ws_context = WsConnectionContext::Create();
    CHECK(ws_context) << "Failed to create websocket context";
    impl_->server_connection_ = ws_context->CreateConnection(
        impl_->config_.operator_server.port,
        impl_->config_.operator_server.addr,
        impl_->config_.operator_server.path,
        impl_->config_.operator_server.security, impl_,
        impl_->config_.operator_server.http_headers);

    CHECK(impl_->server_connection_)
        << "Unable to create websocket connection object";

    impl_->server_connection_->Connect();
  });
}

void Streamer::Unregister() {
  // Usually called from an application thread.
  impl_->signal_thread_->PostTask(
      RTC_FROM_HERE, [this]() { impl_->server_connection_.reset(); });
}

void Streamer::RecordDisplays(LocalRecorder& recorder) {
  for (auto& [key, display] : impl_->displays_) {
    rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> source =
        display.source;
    auto deleter = [](webrtc::VideoTrackSourceInterface* source) {
      source->Release();
    };
    std::shared_ptr<webrtc::VideoTrackSourceInterface> source_shared(
        source.release(), deleter);
    recorder.AddDisplay(display.width, display.height, source_shared);
  }
}

void Streamer::Impl::OnOpen() {
  // Called from the websocket thread.
  // Connected to operator.
  signal_thread_->PostTask(RTC_FROM_HERE, [this]() {
    Json::Value register_obj;
    register_obj[cuttlefish::webrtc_signaling::kTypeField] =
        cuttlefish::webrtc_signaling::kRegisterType;
    register_obj[cuttlefish::webrtc_signaling::kDeviceIdField] =
        config_.device_id;

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
    SendJson(server_connection_.get(), register_obj);
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
  LOG(WARNING) << "Websocket closed unexpectedly";
  signal_thread_->PostTask(RTC_FROM_HERE, [this]() {
    auto observer = operator_observer_.lock();
    if (observer) {
      observer->OnClose();
    }
  });
}

void Streamer::Impl::OnError(const std::string& error) {
  // Called from websocket thread.
  LOG(ERROR) << "Error on connection with the operator: " << error;
  signal_thread_->PostTask(RTC_FROM_HERE, [this]() {
    auto observer = operator_observer_.lock();
    if (observer) {
      observer->OnError();
    }
  });
}

void Streamer::Impl::HandleConfigMessage(const Json::Value& server_message) {
  CHECK(signal_thread_->IsCurrent())
      << __FUNCTION__ << " called from the wrong thread";
  if (server_message.isMember("ice_servers") &&
      server_message["ice_servers"].isArray()) {
    auto servers = server_message["ice_servers"];
    operator_config_.servers.clear();
    for (int server_idx = 0; server_idx < servers.size(); server_idx++) {
      auto server = servers[server_idx];
      webrtc::PeerConnectionInterface::IceServer ice_server;
      if (!server.isMember("urls") || !server["urls"].isArray()) {
        // The urls field is required
        LOG(WARNING)
            << "Invalid ICE server specification obtained from server: "
            << server.toStyledString();
        continue;
      }
      auto urls = server["urls"];
      for (int url_idx = 0; url_idx < urls.size(); url_idx++) {
        auto url = urls[url_idx];
        if (!url.isString()) {
          LOG(WARNING) << "Non string 'urls' field in ice server: "
                       << url.toStyledString();
          continue;
        }
        ice_server.urls.push_back(url.asString());
        if (server.isMember("credential") && server["credential"].isString()) {
          ice_server.password = server["credential"].asString();
        }
        if (server.isMember("username") && server["username"].isString()) {
          ice_server.username = server["username"].asString();
        }
        operator_config_.servers.push_back(ice_server);
      }
    }
  }
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
  signal_thread_->PostTask(RTC_FROM_HERE, [this, server_message]() {
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
      client_id, observer,
      [this, client_id](const Json::Value& msg) {
        SendMessageToClient(client_id, msg);
      },
      [this, client_id] { DestroyClientHandler(client_id); });

  webrtc::PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  config.enable_dtls_srtp = true;
  config.servers.insert(config.servers.end(), operator_config_.servers.begin(),
                        operator_config_.servers.end());
  webrtc::PeerConnectionDependencies dependencies(client_handler.get());
  // PortRangeSocketFactory's super class' constructor needs to be called on the
  // network thread or have it as a parameter
  dependencies.packet_socket_factory.reset(new PortRangeSocketFactory(
      network_thread_.get(), config_.udp_port_range, config_.tcp_port_range));
  auto peer_connection = peer_connection_factory_->CreatePeerConnection(
      config, std::move(dependencies));

  if (!peer_connection) {
    LOG(ERROR) << "Failed to create peer connection";
    return nullptr;
  }

  if (!client_handler->SetPeerConnection(std::move(peer_connection))) {
    return nullptr;
  }

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
  // WsConnection is thread safe
  SendJson(server_connection_.get(), wrapper);
}

void Streamer::Impl::DestroyClientHandler(int client_id) {
  // Usually called from signal thread, could be called from websocket thread or
  // an application thread.
  signal_thread_->PostTask(RTC_FROM_HERE, [this, client_id]() {
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

}  // namespace webrtc_streaming
}  // namespace cuttlefish
