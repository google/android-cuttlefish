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

#define LOG_TAG "ClientHandler"

#include "host/frontend/webrtc/libdevice/client_handler.h"

#include <vector>

#include <json/json.h>
#include <json/writer.h>
#include <netdb.h>
#include <openssl/rand.h>

#include <android-base/logging.h>

#include "host/frontend/webrtc/libcommon/utils.h"
#include "host/frontend/webrtc/libdevice/keyboard.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace webrtc_streaming {

namespace {

static constexpr auto kInputChannelLabel = "input-channel";
static constexpr auto kAdbChannelLabel = "adb-channel";
static constexpr auto kBluetoothChannelLabel = "bluetooth-channel";
static constexpr auto kCameraDataChannelLabel = "camera-data-channel";
static constexpr auto kLocationDataChannelLabel = "location-channel";
static constexpr auto kKmlLocationsDataChannelLabel = "kml-locations-channel";
static constexpr auto kGpxLocationsDataChannelLabel = "gpx-locations-channel";
static constexpr auto kCameraDataEof = "EOF";

}  // namespace

// Video streams initiating in the client may be added and removed at unexpected
// times, causing the webrtc objects to be destroyed and created every time.
// This class hides away that complexity and allows to set up sinks only once.
class ClientVideoTrackImpl : public ClientVideoTrackInterface {
 public:
  void AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame> *sink,
                       const rtc::VideoSinkWants &wants) override {
    sink_ = sink;
    wants_ = wants;
    if (video_track_) {
      video_track_->AddOrUpdateSink(sink, wants);
    }
  }

  void SetVideoTrack(webrtc::VideoTrackInterface *track) {
    video_track_ = track;
    if (sink_) {
      video_track_->AddOrUpdateSink(sink_, wants_);
    }
  }

  void UnsetVideoTrack(webrtc::VideoTrackInterface *track) {
    if (track == video_track_) {
      video_track_ = nullptr;
    }
  }

 private:
  webrtc::VideoTrackInterface* video_track_;
  rtc::VideoSinkInterface<webrtc::VideoFrame> *sink_ = nullptr;
  rtc::VideoSinkWants wants_ = {};
};

class InputChannelHandler : public webrtc::DataChannelObserver {
 public:
  InputChannelHandler(
      rtc::scoped_refptr<webrtc::DataChannelInterface> input_channel,
      std::shared_ptr<ConnectionObserver> observer);
  ~InputChannelHandler() override;

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer &msg) override;

 private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> input_channel_;
  std::shared_ptr<ConnectionObserver> observer_;
};

class AdbChannelHandler : public webrtc::DataChannelObserver {
 public:
  AdbChannelHandler(
      rtc::scoped_refptr<webrtc::DataChannelInterface> adb_channel,
      std::shared_ptr<ConnectionObserver> observer);
  ~AdbChannelHandler() override;

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer &msg) override;

 private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> adb_channel_;
  std::shared_ptr<ConnectionObserver> observer_;
  bool channel_open_reported_ = false;
};

class ControlChannelHandler : public webrtc::DataChannelObserver {
 public:
  ControlChannelHandler(
      rtc::scoped_refptr<webrtc::DataChannelInterface> control_channel,
      std::shared_ptr<ConnectionObserver> observer);
  ~ControlChannelHandler() override;

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer &msg) override;

  bool Send(const Json::Value &message);
  bool Send(const uint8_t *msg, size_t size, bool binary);

 private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> control_channel_;
  std::shared_ptr<ConnectionObserver> observer_;
};

class BluetoothChannelHandler : public webrtc::DataChannelObserver {
 public:
  BluetoothChannelHandler(
      rtc::scoped_refptr<webrtc::DataChannelInterface> bluetooth_channel,
      std::shared_ptr<ConnectionObserver> observer);
  ~BluetoothChannelHandler() override;

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer &msg) override;

 private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> bluetooth_channel_;
  std::shared_ptr<ConnectionObserver> observer_;
  bool channel_open_reported_ = false;
};

class LocationChannelHandler : public webrtc::DataChannelObserver {
 public:
  LocationChannelHandler(
      rtc::scoped_refptr<webrtc::DataChannelInterface> location_channel,
      std::shared_ptr<ConnectionObserver> observer);
  ~LocationChannelHandler() override;

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer &msg) override;

 private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> location_channel_;
  std::shared_ptr<ConnectionObserver> observer_;
  bool channel_open_reported_ = false;
};

class KmlLocationsChannelHandler : public webrtc::DataChannelObserver {
 public:
  KmlLocationsChannelHandler(
      rtc::scoped_refptr<webrtc::DataChannelInterface> kml_locations_channel,
      std::shared_ptr<ConnectionObserver> observer);
  ~KmlLocationsChannelHandler() override;

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer &msg) override;

 private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> kml_locations_channel_;
  std::shared_ptr<ConnectionObserver> observer_;
  bool channel_open_reported_ = false;
};

class GpxLocationsChannelHandler : public webrtc::DataChannelObserver {
 public:
  GpxLocationsChannelHandler(
      rtc::scoped_refptr<webrtc::DataChannelInterface> gpx_locations_channel,
      std::shared_ptr<ConnectionObserver> observer);
  ~GpxLocationsChannelHandler() override;

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer &msg) override;

 private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> gpx_locations_channel_;
  std::shared_ptr<ConnectionObserver> observer_;
  bool channel_open_reported_ = false;
};

class CameraChannelHandler : public webrtc::DataChannelObserver {
 public:
  CameraChannelHandler(
      rtc::scoped_refptr<webrtc::DataChannelInterface> bluetooth_channel,
      std::shared_ptr<ConnectionObserver> observer);
  ~CameraChannelHandler() override;

  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer &msg) override;

 private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> camera_channel_;
  std::shared_ptr<ConnectionObserver> observer_;
  std::vector<char> receive_buffer_;
};

InputChannelHandler::InputChannelHandler(
    rtc::scoped_refptr<webrtc::DataChannelInterface> input_channel,
    std::shared_ptr<ConnectionObserver> observer)
    : input_channel_(input_channel), observer_(observer) {
  input_channel->RegisterObserver(this);
}

InputChannelHandler::~InputChannelHandler() {
  input_channel_->UnregisterObserver();
}

void InputChannelHandler::OnStateChange() {
  LOG(VERBOSE) << "Input channel state changed to "
               << webrtc::DataChannelInterface::DataStateString(
                      input_channel_->state());
}

void InputChannelHandler::OnMessage(const webrtc::DataBuffer &msg) {
  if (msg.binary) {
    // TODO (jemoreira) consider binary protocol to avoid JSON parsing overhead
    LOG(ERROR) << "Received invalid (binary) data on input channel";
    return;
  }
  auto size = msg.size();

  Json::Value evt;
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> json_reader(builder.newCharReader());
  std::string errorMessage;
  auto str = msg.data.cdata<char>();
  if (!json_reader->parse(str, str + size, &evt, &errorMessage) < 0) {
    LOG(ERROR) << "Received invalid JSON object over input channel: "
               << errorMessage;
    return;
  }
  if (!evt.isMember("type") || !evt["type"].isString()) {
    LOG(ERROR) << "Input event doesn't have a valid 'type' field: "
               << evt.toStyledString();
    return;
  }
  auto event_type = evt["type"].asString();
  if (event_type == "mouse") {
    auto result =
        ValidateJsonObject(evt, "mouse",
                           {{"down", Json::ValueType::intValue},
                            {"x", Json::ValueType::intValue},
                            {"y", Json::ValueType::intValue},
                            {"display_label", Json::ValueType::stringValue}});
    if (!result.ok()) {
      LOG(ERROR) << result.error().Trace();
      return;
    }
    auto label = evt["display_label"].asString();
    int32_t down = evt["down"].asInt();
    int32_t x = evt["x"].asInt();
    int32_t y = evt["y"].asInt();

    observer_->OnTouchEvent(label, x, y, down);
  } else if (event_type == "multi-touch") {
    auto result =
        ValidateJsonObject(evt, "multi-touch",
                           {{"id", Json::ValueType::arrayValue},
                            {"down", Json::ValueType::intValue},
                            {"x", Json::ValueType::arrayValue},
                            {"y", Json::ValueType::arrayValue},
                            {"slot", Json::ValueType::arrayValue},
                            {"display_label", Json::ValueType::stringValue}});
    if (!result.ok()) {
      LOG(ERROR) << result.error().Trace();
      return;
    }

    auto label = evt["display_label"].asString();
    auto idArr = evt["id"];
    int32_t down = evt["down"].asInt();
    auto xArr = evt["x"];
    auto yArr = evt["y"];
    auto slotArr = evt["slot"];
    int size = evt["id"].size();

    observer_->OnMultiTouchEvent(label, idArr, slotArr, xArr, yArr, down, size);
  } else if (event_type == "keyboard") {
    auto result =
        ValidateJsonObject(evt, "keyboard",
                           {{"event_type", Json::ValueType::stringValue},
                            {"keycode", Json::ValueType::stringValue}});
    if (!result.ok()) {
      LOG(ERROR) << result.error().Trace();
      return;
    }
    auto down = evt["event_type"].asString() == std::string("keydown");
    auto code = DomKeyCodeToLinux(evt["keycode"].asString());
    observer_->OnKeyboardEvent(code, down);
  } else {
    LOG(ERROR) << "Unrecognized event type: " << event_type;
    return;
  }
}

AdbChannelHandler::AdbChannelHandler(
    rtc::scoped_refptr<webrtc::DataChannelInterface> adb_channel,
    std::shared_ptr<ConnectionObserver> observer)
    : adb_channel_(adb_channel), observer_(observer) {
  adb_channel->RegisterObserver(this);
}

AdbChannelHandler::~AdbChannelHandler() { adb_channel_->UnregisterObserver(); }

void AdbChannelHandler::OnStateChange() {
  LOG(VERBOSE) << "Adb channel state changed to "
               << webrtc::DataChannelInterface::DataStateString(
                      adb_channel_->state());
}

void AdbChannelHandler::OnMessage(const webrtc::DataBuffer &msg) {
  // Report the adb channel as open on the first message received instead of at
  // channel open, this avoids unnecessarily connecting to the adb daemon for
  // clients that don't use ADB.
  if (!channel_open_reported_) {
    observer_->OnAdbChannelOpen([this](const uint8_t *msg, size_t size) {
      webrtc::DataBuffer buffer(rtc::CopyOnWriteBuffer(msg, size),
                                true /*binary*/);
      // TODO (b/185832105): When the SCTP channel is congested data channel
      // messages are buffered up to 16MB, when the buffer is full the channel
      // is abruptly closed. Keep track of the buffered data to avoid losing the
      // adb data channel.
      return adb_channel_->Send(buffer);
    });
    channel_open_reported_ = true;
  }
  observer_->OnAdbMessage(msg.data.cdata(), msg.size());
}

ControlChannelHandler::ControlChannelHandler(
    rtc::scoped_refptr<webrtc::DataChannelInterface> control_channel,
    std::shared_ptr<ConnectionObserver> observer)
    : control_channel_(control_channel), observer_(observer) {
  control_channel->RegisterObserver(this);
}

ControlChannelHandler::~ControlChannelHandler() {
  control_channel_->UnregisterObserver();
}

void ControlChannelHandler::OnStateChange() {
  auto state = control_channel_->state();
  LOG(VERBOSE) << "Control channel state changed to "
               << webrtc::DataChannelInterface::DataStateString(state);
  if (state == webrtc::DataChannelInterface::kOpen) {
    observer_->OnControlChannelOpen(
        [this](const Json::Value &message) { return this->Send(message); });
  }
}

void ControlChannelHandler::OnMessage(const webrtc::DataBuffer &msg) {
  auto msg_str = msg.data.cdata<char>();
  auto size = msg.size();
  Json::Value evt;
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> json_reader(builder.newCharReader());
  std::string errorMessage;
  if (!json_reader->parse(msg_str, msg_str + size, &evt, &errorMessage)) {
    LOG(ERROR) << "Received invalid JSON object over control channel: "
               << errorMessage;
    return;
  }

  auto result = ValidateJsonObject(
      evt, "command",
      /*required_fields=*/{{"command", Json::ValueType::stringValue}},
      /*optional_fields=*/
      {
          {"button_state", Json::ValueType::stringValue},
          {"lid_switch_open", Json::ValueType::booleanValue},
          {"hinge_angle_value", Json::ValueType::intValue},
      });
  if (!result.ok()) {
    LOG(ERROR) << result.error().Trace();
    return;
  }
  auto command = evt["command"].asString();

  if (command == "device_state") {
    if (evt.isMember("lid_switch_open")) {
      observer_->OnLidStateChange(evt["lid_switch_open"].asBool());
    }
    if (evt.isMember("hinge_angle_value")) {
      observer_->OnHingeAngleChange(evt["hinge_angle_value"].asInt());
    }
    return;
  } else if (command.rfind("camera_", 0) == 0) {
    observer_->OnCameraControlMsg(evt);
    return;
  }

  auto button_state = evt["button_state"].asString();
  LOG(VERBOSE) << "Control command: " << command << " (" << button_state << ")";
  if (command == "power") {
    observer_->OnPowerButton(button_state == "down");
  } else if (command == "back") {
    observer_->OnBackButton(button_state == "down");
  } else if (command == "home") {
    observer_->OnHomeButton(button_state == "down");
  } else if (command == "menu") {
    observer_->OnMenuButton(button_state == "down");
  } else if (command == "volumedown") {
    observer_->OnVolumeDownButton(button_state == "down");
  } else if (command == "volumeup") {
    observer_->OnVolumeUpButton(button_state == "down");
  } else {
    observer_->OnCustomActionButton(command, button_state);
  }
}

bool ControlChannelHandler::Send(const Json::Value& message) {
  Json::StreamWriterBuilder factory;
  std::string message_string = Json::writeString(factory, message);
  return Send(reinterpret_cast<const uint8_t*>(message_string.c_str()),
       message_string.size(), /*binary=*/false);
}

bool ControlChannelHandler::Send(const uint8_t *msg, size_t size, bool binary) {
  webrtc::DataBuffer buffer(rtc::CopyOnWriteBuffer(msg, size), binary);
  return control_channel_->Send(buffer);
}

BluetoothChannelHandler::BluetoothChannelHandler(
    rtc::scoped_refptr<webrtc::DataChannelInterface> bluetooth_channel,
    std::shared_ptr<ConnectionObserver> observer)
    : bluetooth_channel_(bluetooth_channel), observer_(observer) {
  bluetooth_channel_->RegisterObserver(this);
}

BluetoothChannelHandler::~BluetoothChannelHandler() {
  bluetooth_channel_->UnregisterObserver();
}

void BluetoothChannelHandler::OnStateChange() {
  LOG(VERBOSE) << "Bluetooth channel state changed to "
               << webrtc::DataChannelInterface::DataStateString(
                      bluetooth_channel_->state());
}

void BluetoothChannelHandler::OnMessage(const webrtc::DataBuffer &msg) {
  // Notify bluetooth channel opening when actually using the channel,
  // it has the same reason with AdbChannelHandler::OnMessage,
  // to avoid unnecessarily connection for Rootcanal.
  if (channel_open_reported_ == false) {
    channel_open_reported_ = true;
    observer_->OnBluetoothChannelOpen([this](const uint8_t *msg, size_t size) {
      webrtc::DataBuffer buffer(rtc::CopyOnWriteBuffer(msg, size),
                                true /*binary*/);
      // TODO (b/185832105): When the SCTP channel is congested data channel
      // messages are buffered up to 16MB, when the buffer is full the channel
      // is abruptly closed. Keep track of the buffered data to avoid losing the
      // adb data channel.
      return bluetooth_channel_->Send(buffer);
    });
  }

  observer_->OnBluetoothMessage(msg.data.cdata(), msg.size());
}

LocationChannelHandler::LocationChannelHandler(
    rtc::scoped_refptr<webrtc::DataChannelInterface> location_channel,
    std::shared_ptr<ConnectionObserver> observer)
    : location_channel_(location_channel), observer_(observer) {
  location_channel_->RegisterObserver(this);
}

LocationChannelHandler::~LocationChannelHandler() {
  location_channel_->UnregisterObserver();
}

void LocationChannelHandler::OnStateChange() {
  LOG(VERBOSE) << "Location channel state changed to "
               << webrtc::DataChannelInterface::DataStateString(
                      location_channel_->state());
}

void LocationChannelHandler::OnMessage(const webrtc::DataBuffer &msg) {
  // Notify location channel opening when actually using the channel,
  // it has the same reason with AdbChannelHandler::OnMessage,
  // to avoid unnecessarily connection for Rootcanal.
  if (channel_open_reported_ == false) {
    channel_open_reported_ = true;
    observer_->OnLocationChannelOpen([this](const uint8_t *msg, size_t size) {
      webrtc::DataBuffer buffer(rtc::CopyOnWriteBuffer(msg, size),
                                true /*binary*/);
      // messages are buffered up to 16MB, when the buffer is full the channel
      // is abruptly closed. Keep track of the buffered data to avoid losing the
      // adb data channel.
      location_channel_->Send(buffer);
      return true;
    });
  }

  observer_->OnLocationMessage(msg.data.cdata(), msg.size());
}

KmlLocationsChannelHandler::KmlLocationsChannelHandler(
    rtc::scoped_refptr<webrtc::DataChannelInterface> kml_locations_channel,
    std::shared_ptr<ConnectionObserver> observer)
    : kml_locations_channel_(kml_locations_channel), observer_(observer) {
  kml_locations_channel_->RegisterObserver(this);
}

KmlLocationsChannelHandler::~KmlLocationsChannelHandler() {
  kml_locations_channel_->UnregisterObserver();
}

void KmlLocationsChannelHandler::OnStateChange() {
  LOG(VERBOSE) << "KmlLocations channel state changed to "
               << webrtc::DataChannelInterface::DataStateString(
                      kml_locations_channel_->state());
}

void KmlLocationsChannelHandler::OnMessage(const webrtc::DataBuffer &msg) {
  // Notify kml_locations channel opening when actually using the channel,
  // it has the same reason with AdbChannelHandler::OnMessage,
  // to avoid unnecessarily connection for Rootcanal.
  if (channel_open_reported_ == false) {
    channel_open_reported_ = true;
    observer_->OnKmlLocationsChannelOpen(
        [this](const uint8_t *msg, size_t size) {
          webrtc::DataBuffer buffer(rtc::CopyOnWriteBuffer(msg, size),
                                    true /*binary*/);
          kml_locations_channel_->Send(buffer);
          return true;
        });
  }

  observer_->OnKmlLocationsMessage(msg.data.cdata(), msg.size());
}

GpxLocationsChannelHandler::GpxLocationsChannelHandler(
    rtc::scoped_refptr<webrtc::DataChannelInterface> gpx_locations_channel,
    std::shared_ptr<ConnectionObserver> observer)
    : gpx_locations_channel_(gpx_locations_channel), observer_(observer) {
  gpx_locations_channel_->RegisterObserver(this);
}

GpxLocationsChannelHandler::~GpxLocationsChannelHandler() {
  gpx_locations_channel_->UnregisterObserver();
}

void GpxLocationsChannelHandler::OnStateChange() {
  LOG(VERBOSE) << "GpxLocations channel state changed to "
               << webrtc::DataChannelInterface::DataStateString(
                      gpx_locations_channel_->state());
}

void GpxLocationsChannelHandler::OnMessage(const webrtc::DataBuffer &msg) {
  // Notify gpx_locations channel opening when actually using the channel,
  // it has the same reason with AdbChannelHandler::OnMessage,
  // to avoid unnecessarily connection for Rootcanal.
  if (channel_open_reported_ == false) {
    channel_open_reported_ = true;
    observer_->OnGpxLocationsChannelOpen(
        [this](const uint8_t *msg, size_t size) {
          webrtc::DataBuffer buffer(rtc::CopyOnWriteBuffer(msg, size),
                                    true /*binary*/);
          gpx_locations_channel_->Send(buffer);
          return true;
        });
  }

  observer_->OnGpxLocationsMessage(msg.data.cdata(), msg.size());
}

CameraChannelHandler::CameraChannelHandler(
    rtc::scoped_refptr<webrtc::DataChannelInterface> camera_channel,
    std::shared_ptr<ConnectionObserver> observer)
    : camera_channel_(camera_channel), observer_(observer) {
  camera_channel_->RegisterObserver(this);
}

CameraChannelHandler::~CameraChannelHandler() {
  camera_channel_->UnregisterObserver();
}

void CameraChannelHandler::OnStateChange() {
  LOG(VERBOSE) << "Camera channel state changed to "
               << webrtc::DataChannelInterface::DataStateString(
                      camera_channel_->state());
}

void CameraChannelHandler::OnMessage(const webrtc::DataBuffer &msg) {
  auto msg_data = msg.data.cdata<char>();
  if (msg.size() == strlen(kCameraDataEof) &&
      !strncmp(msg_data, kCameraDataEof, msg.size())) {
    // Send complete buffer to observer on EOF marker
    observer_->OnCameraData(receive_buffer_);
    receive_buffer_.clear();
    return;
  }
  // Otherwise buffer up data
  receive_buffer_.insert(receive_buffer_.end(), msg_data,
                         msg_data + msg.size());
}

std::shared_ptr<ClientHandler> ClientHandler::Create(
    int client_id, std::shared_ptr<ConnectionObserver> observer,
    PeerConnectionBuilder &connection_builder,
    std::function<void(const Json::Value &)> send_to_client_cb,
    std::function<void(bool)> on_connection_changed_cb) {
  return std::shared_ptr<ClientHandler>(
      new ClientHandler(client_id, observer, connection_builder,
                        send_to_client_cb, on_connection_changed_cb));
}

ClientHandler::ClientHandler(
    int client_id, std::shared_ptr<ConnectionObserver> observer,
    PeerConnectionBuilder &connection_builder,
    std::function<void(const Json::Value &)> send_to_client_cb,
    std::function<void(bool)> on_connection_changed_cb)
    : client_id_(client_id),
      observer_(observer),
      send_to_client_(send_to_client_cb),
      on_connection_changed_cb_(on_connection_changed_cb),
      connection_builder_(connection_builder),
      controller_(*this, *this, *this),
      camera_track_(new ClientVideoTrackImpl()) {}

ClientHandler::~ClientHandler() {
  for (auto &data_channel : data_channels_) {
    data_channel->UnregisterObserver();
  }
}

rtc::scoped_refptr<webrtc::RtpSenderInterface>
ClientHandler::AddTrackToConnection(
    rtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track,
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection,
    const std::string &label) {
  if (!peer_connection) {
    return nullptr;
  }
  // Send each track as part of a different stream with the label as id
  auto err_or_sender =
      peer_connection->AddTrack(track, {label} /* stream_id */);
  if (!err_or_sender.ok()) {
    LOG(ERROR) << "Failed to add track to the peer connection";
    return nullptr;
  }
  return err_or_sender.MoveValue();
}

bool ClientHandler::AddDisplay(
    rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track,
    const std::string &label) {
  auto [it, inserted] = displays_.emplace(label, DisplayTrackAndSender{
                                                     .track = video_track,
                                                 });
  auto sender =
      AddTrackToConnection(video_track, controller_.peer_connection(), label);
  if (sender) {
    DisplayTrackAndSender &info = it->second;
    info.sender = sender;
  }
  // Succeed if the peer connection is null or the track was added
  return controller_.peer_connection() == nullptr || sender;
}

bool ClientHandler::RemoveDisplay(const std::string &label) {
  auto it = displays_.find(label);
  if (it == displays_.end()) {
    return false;
  }

  if (controller_.peer_connection()) {
    DisplayTrackAndSender &info = it->second;

    bool success = controller_.peer_connection()->RemoveTrack(info.sender);
    if (!success) {
      LOG(ERROR) << "Failed to remove video track for display: " << label;
      return false;
    }
  }

  displays_.erase(it);
  return true;
}

bool ClientHandler::AddAudio(
    rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track,
    const std::string &label) {
  audio_streams_.emplace_back(audio_track, label);
  auto peer_connection = controller_.peer_connection();
  if (!peer_connection) {
    return true;
  }
  return AddTrackToConnection(audio_track, controller_.peer_connection(),
                              label);
}

ClientVideoTrackInterface* ClientHandler::GetCameraStream() {
  return camera_track_.get();
}

Result<void> ClientHandler::SendMessage(const Json::Value &msg) {
  send_to_client_(msg);
  return {};
}

Result<rtc::scoped_refptr<webrtc::PeerConnectionInterface>>
ClientHandler::Build(
    webrtc::PeerConnectionObserver &observer,
    const std::vector<webrtc::PeerConnectionInterface::IceServer>
        &per_connection_servers) {
  auto peer_connection =
      CF_EXPECT(connection_builder_.Build(observer, per_connection_servers));

  // Re-add the video and audio tracks after the peer connection has been
  // created
  for (auto &[label, info] : displays_) {
    info.sender =
        CF_EXPECT(AddTrackToConnection(info.track, peer_connection, label));
  }
  // Add the audio tracks to the peer connection
  for (auto &[audio_track, label] : audio_streams_) {
    // Audio channels are never removed from the connection by the device, so
    // it's ok to discard the returned sender here. The peer connection keeps
    // track of it anyways.
    CF_EXPECT(AddTrackToConnection(audio_track, peer_connection, label));
  }

  // libwebrtc configures the video encoder with a start bitrate of just 300kbs
  // which causes it to drop the first 4 frames it receives. Any value over 2Mbs
  // will be capped at 2Mbs when passed to the encoder by the peer_connection
  // object, so we pass the maximum possible value here.
  webrtc::BitrateSettings bitrate_settings;
  bitrate_settings.start_bitrate_bps = 2000000;  // 2Mbs
  peer_connection->SetBitrate(bitrate_settings);

  // At least one data channel needs to be created on the side that creates the
  // SDP offer (the device) for data channels to be enabled at all.
  // This channel is meant to carry control commands from the client.
  auto control_channel = peer_connection->CreateDataChannel(
      "device-control", nullptr /* config */);
  CF_EXPECT(control_channel.get(), "Failed to create control data channel");
  control_handler_.reset(new ControlChannelHandler(control_channel, observer_));

  return peer_connection;
}

void ClientHandler::HandleMessage(const Json::Value &message) {
  controller_.HandleSignalingMessage(message);
}

void ClientHandler::Close() {
  // We can't simply call peer_connection_->Close() here because this method
  // could be called from one of the PeerConnectionObserver callbacks and that
  // would lead to a deadlock (Close eventually tries to destroy an object that
  // will then wait for the callback to return -> deadlock). Destroying the
  // peer_connection_ has the same effect. The only alternative is to postpone
  // that operation until after the callback returns.
  on_connection_changed_cb_(false);
}

void ClientHandler::OnConnectionStateChange(
    Result<webrtc::PeerConnectionInterface::PeerConnectionState> new_state) {
  if (!new_state.ok()) {
    LOG(ERROR) << "Connection error: " << new_state.error().Message();
    LOG(DEBUG) << new_state.error().Trace();
    Close();
    return;
  }
  switch (*new_state) {
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
      LOG(VERBOSE) << "Client " << client_id_ << ": WebRTC connected";
      observer_->OnConnected();
      on_connection_changed_cb_(true);
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
      LOG(VERBOSE) << "Client " << client_id_ << ": Connection disconnected";
      Close();
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
      LOG(ERROR) << "Client " << client_id_ << ": Connection failed";
      Close();
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
      LOG(VERBOSE) << "Client " << client_id_ << ": Connection closed";
      Close();
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kNew:
      LOG(VERBOSE) << "Client " << client_id_ << ": Connection new";
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
      LOG(VERBOSE) << "Client " << client_id_ << ": Connection started";
      break;
  }
}

void ClientHandler::OnDataChannel(
    rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) {
  auto label = data_channel->label();
  if (label == kInputChannelLabel) {
    input_handler_.reset(new InputChannelHandler(data_channel, observer_));
  } else if (label == kAdbChannelLabel) {
    adb_handler_.reset(new AdbChannelHandler(data_channel, observer_));
  } else if (label == kBluetoothChannelLabel) {
    bluetooth_handler_.reset(
        new BluetoothChannelHandler(data_channel, observer_));
  } else if (label == kCameraDataChannelLabel) {
    camera_data_handler_.reset(
        new CameraChannelHandler(data_channel, observer_));
  } else if (label == kLocationDataChannelLabel) {
    location_handler_.reset(
        new LocationChannelHandler(data_channel, observer_));
  } else if (label == kKmlLocationsDataChannelLabel) {
    kml_location_handler_.reset(
        new KmlLocationsChannelHandler(data_channel, observer_));
  } else if (label == kGpxLocationsDataChannelLabel) {
    gpx_location_handler_.reset(
        new GpxLocationsChannelHandler(data_channel, observer_));
  } else {
    LOG(VERBOSE) << "Data channel connected: " << label;
    data_channels_.push_back(data_channel);
  }
}

void ClientHandler::OnTrack(
    rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
  auto track = transceiver->receiver()->track();
  if (track && track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    // It's ok to take the raw pointer here because we make sure to unset it
    // when the track is removed
    camera_track_->SetVideoTrack(
        static_cast<webrtc::VideoTrackInterface *>(track.get()));
  }
}
void ClientHandler::OnRemoveTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
  auto track = receiver->track();
  if (track && track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
    // this only unsets if the track matches the one already in store
    camera_track_->UnsetVideoTrack(
        reinterpret_cast<webrtc::VideoTrackInterface *>(track.get()));
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
