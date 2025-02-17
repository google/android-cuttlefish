/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/frontend/webrtc/libdevice/data_channels.h"

#include <android-base/logging.h>

#include "common/libs/utils/json.h"
#include "host/frontend/webrtc/libcommon/utils.h"
#include "host/frontend/webrtc/libdevice/keyboard.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace webrtc_streaming {

class DataChannelHandler : public webrtc::DataChannelObserver {
 public:
  virtual ~DataChannelHandler() = default;

  bool Send(const uint8_t *msg, size_t size, bool binary);
  bool Send(const Json::Value &message);

  // webrtc::DataChannelObserver implementation
  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer &msg) override;

 protected:
  // Provide access to the underlying data channel and the connection observer.
  virtual rtc::scoped_refptr<webrtc::DataChannelInterface> channel() = 0;
  virtual std::shared_ptr<ConnectionObserver> observer() = 0;

  // Subclasses must override this to process messages.
  virtual Result<void> OnMessageInner(const webrtc::DataBuffer &msg) = 0;
  // Some subclasses may override this to defer some work until the channel is
  // actually used.
  virtual void OnFirstMessage() {}
  virtual void OnStateChangeInner(webrtc::DataChannelInterface::DataState) {}

  std::function<bool(const uint8_t *, size_t len)> GetBinarySender() {
    return [this](const uint8_t *msg, size_t size) {
      return Send(msg, size, true /*binary*/);
    };
  }
  std::function<bool(const Json::Value &)> GetJSONSender() {
    return [this](const Json::Value &msg) { return Send(msg); };
  }

 private:
  bool first_msg_received_ = false;
};

namespace {

static constexpr auto kInputChannelLabel = "input-channel";
static constexpr auto kAdbChannelLabel = "adb-channel";
static constexpr auto kBluetoothChannelLabel = "bluetooth-channel";
static constexpr auto kCameraDataChannelLabel = "camera-data-channel";
static constexpr auto kSensorsDataChannelLabel = "sensors-channel";
static constexpr auto kLightsChannelLabel = "lights-channel";
static constexpr auto kLocationDataChannelLabel = "location-channel";
static constexpr auto kKmlLocationsDataChannelLabel = "kml-locations-channel";
static constexpr auto kGpxLocationsDataChannelLabel = "gpx-locations-channel";
static constexpr auto kCameraDataEof = "EOF";

// These classes use the Template pattern to minimize code repetition between
// data channel handlers.

class InputChannelHandler : public DataChannelHandler {
 public:
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    // TODO: jemoreira - consider binary protocol to avoid JSON parsing
    // overhead
    CF_EXPECT(!msg.binary, "Received invalid (binary) data on input channel");
    auto size = msg.size();

    Json::Value evt;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> json_reader(builder.newCharReader());
    std::string error_message;
    auto str = msg.data.cdata<char>();
    CF_EXPECTF(json_reader->parse(str, str + size, &evt, &error_message),
               "Received invalid JSON object over control channel: '{}'",
               error_message);

    auto event_type = CF_EXPECT(GetValue<std::string>(evt, {"type"}),
                                "Failed to get property 'type' from message");
    auto get_or_err = [&event_type,
                       &evt]<typename T>(const std::string &prop) -> Result<T> {
      return CF_EXPECTF(GetValue<T>(evt, {prop}),
                        "Failed to get property '{}' from '{}' message", prop,
                        event_type);
    };
    auto get_int = [get_or_err](auto prop) -> Result<int> {
      return get_or_err.operator()<int>(prop);
    };
    auto get_str = [get_or_err](auto prop) -> Result<std::string> {
      return get_or_err.operator()<std::string>(prop);
    };
    auto get_arr = [get_or_err, &event_type](
                              const std::string &prop) -> Result<Json::Value> {
      Json::Value arr = CF_EXPECT(get_or_err.operator()<Json::Value>(prop));
      CF_EXPECTF(arr.isArray(), "Property '{}' of '{}' message is not an array",
                 prop, event_type);
      return arr;
    };

    if (event_type == "mouseMove") {
      int32_t x = CF_EXPECT(get_int("x"));
      int32_t y = CF_EXPECT(get_int("y"));

      CF_EXPECT(observer()->OnMouseMoveEvent(x, y));
    } else if (event_type == "mouseButton") {
      int32_t button = CF_EXPECT(get_int("button"));
      int32_t down = CF_EXPECT(get_int("down"));

      CF_EXPECT(observer()->OnMouseButtonEvent(button, down));
    } else if (event_type == "mouseWheel") {
      int pixels = CF_EXPECT(get_int("pixels"));

      CF_EXPECT(observer()->OnMouseWheelEvent(pixels));
    } else if (event_type == "multi-touch") {
      std::string label = CF_EXPECT(get_str("device_label"));
      auto idArr = CF_EXPECT(get_arr("id"));
      int32_t down = CF_EXPECT(get_int("down"));
      auto xArr = CF_EXPECT(get_arr("x"));
      auto yArr = CF_EXPECT(get_arr("y"));
      int size = idArr.size();

      CF_EXPECT(
          observer()->OnMultiTouchEvent(label, idArr, xArr, yArr, down, size));
    } else if (event_type == "keyboard") {
      auto cvd_config =
          CF_EXPECT(CuttlefishConfig::Get(), "CuttlefishConfig is null!");
      auto instance = cvd_config->ForDefaultInstance();
      Json::Value domkey_mapping_config_json = instance.domkey_mapping_config();
      bool down = CF_EXPECT(get_str("event_type")) == std::string("keydown");
      std::string keycode = CF_EXPECT(get_str("keycode"));
      uint16_t code = DomKeyCodeToLinux(keycode);
      if (domkey_mapping_config_json.isMember("mappings") &&
          domkey_mapping_config_json["mappings"].isMember(keycode)) {
        code = domkey_mapping_config_json["mappings"][keycode].asUInt();
      }

      CF_EXPECT(observer()->OnKeyboardEvent(code, down));
    } else if (event_type == "wheel") {
      int pixels = CF_EXPECT(get_int("pixels"));

      CF_EXPECT(observer()->OnRotaryWheelEvent(pixels));
    } else {
      return CF_ERRF("Unrecognized event type: '{}'", event_type);
    }
    return {};
  }
};

class ControlChannelHandler : public DataChannelHandler {
 public:
  void OnStateChangeInner(
      webrtc::DataChannelInterface::DataState state) override {
    if (state == webrtc::DataChannelInterface::kOpen) {
      observer()->OnControlChannelOpen(GetJSONSender());
    }
  }
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    auto msg_str = msg.data.cdata<char>();
    auto size = msg.size();
    Json::Value evt;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> json_reader(builder.newCharReader());
    std::string error_message;
    CF_EXPECTF(
        json_reader->parse(msg_str, msg_str + size, &evt, &error_message),
        "Received invalid JSON object over control channel: '{}'",
        error_message);

    auto command =
        CF_EXPECT(GetValue<std::string>(evt, {"command"}),
                  "Failed to access 'command' property on control message");

    if (command == "device_state") {
      if (evt.isMember("lid_switch_open")) {
        CF_EXPECT(observer()->OnLidStateChange(
            CF_EXPECT(GetValue<bool>(evt, {"lid_switch_open"}))));
      }
      if (evt.isMember("hinge_angle_value")) {
        observer()->OnHingeAngleChange(
            CF_EXPECT(GetValue<int>(evt, {"hinge_angle_value"})));
      }
      return {};
    } else if (command.rfind("camera_", 0) == 0) {
      observer()->OnCameraControlMsg(evt);
      return {};
    } else if (command == "display") {
      observer()->OnDisplayControlMsg(evt);
      return {};
    } else if (command == "add-display") {
      observer()->OnDisplayAddMsg(evt);
      return {};
    } else if (command == "remove-display") {
      observer()->OnDisplayRemoveMsg(evt);
      return {};
    }

    auto button_state =
        CF_EXPECT(GetValue<std::string>(evt, {"button_state"}),
                  "Failed to get 'button_state' property of control message");
    LOG(VERBOSE) << "Control command: " << command << " (" << button_state
                 << ")";

    if (command == "power") {
      CF_EXPECT(observer()->OnPowerButton(button_state == "down"));
    } else if (command == "back") {
      CF_EXPECT(observer()->OnBackButton(button_state == "down"));
    } else if (command == "home") {
      CF_EXPECT(observer()->OnHomeButton(button_state == "down"));
    } else if (command == "menu") {
      CF_EXPECT(observer()->OnMenuButton(button_state == "down"));
    } else if (command == "volumedown") {
      CF_EXPECT(observer()->OnVolumeDownButton(button_state == "down"));
    } else if (command == "volumeup") {
      CF_EXPECT(observer()->OnVolumeUpButton(button_state == "down"));
    } else {
      observer()->OnCustomActionButton(command, button_state);
    }
    return {};
  }
};

class AdbChannelHandler : public DataChannelHandler {
 public:
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnAdbMessage(msg.data.cdata(), msg.size());
    return {};
  }
  void OnFirstMessage() override {
    // Report the adb channel as open on the first message received instead of
    // at channel open, this avoids unnecessarily connecting to the adb daemon
    // for clients that don't use ADB.
    observer()->OnAdbChannelOpen(GetBinarySender());
  }
};

class BluetoothChannelHandler : public DataChannelHandler {
 public:
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnBluetoothMessage(msg.data.cdata(), msg.size());
    return {};
  }
  void OnFirstMessage() override {
    // Notify bluetooth channel opening when actually using the channel,
    // it has the same reason with AdbChannelHandler::OnMessageInner,
    // to avoid unnecessary connection for Rootcanal.
    observer()->OnBluetoothChannelOpen(GetBinarySender());
  }
};

class CameraChannelHandler : public DataChannelHandler {
 public:
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    auto msg_data = msg.data.cdata<char>();
    if (msg.size() == strlen(kCameraDataEof) &&
        !strncmp(msg_data, kCameraDataEof, msg.size())) {
      // Send complete buffer to observer on EOF marker
      observer()->OnCameraData(receive_buffer_);
      receive_buffer_.clear();
      return {};
    }
    // Otherwise buffer up data
    receive_buffer_.insert(receive_buffer_.end(), msg_data,
                           msg_data + msg.size());
    return {};
  }

 private:
  std::vector<char> receive_buffer_;
};

// TODO(b/297361564)
class SensorsChannelHandler : public DataChannelHandler {
 public:
  void OnFirstMessage() override {
    observer()->OnSensorsChannelOpen(GetBinarySender());
  }
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    if (!first_msg_received_) {
      first_msg_received_ = true;
      return {};
    }
    observer()->OnSensorsMessage(msg.data.cdata(), msg.size());
    return {};
  }

  void OnStateChangeInner(
      webrtc::DataChannelInterface::DataState state) override {
    if (state == webrtc::DataChannelInterface::kClosed) {
      observer()->OnSensorsChannelClosed();
    }
  }

 private:
  bool first_msg_received_ = false;
};

class LightsChannelHandler : public DataChannelHandler {
 public:
  // We do not expect any messages from the frontend.
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    return {};
  }

  void OnStateChangeInner(
      webrtc::DataChannelInterface::DataState state) override {
    if (state == webrtc::DataChannelInterface::kOpen) {
      observer()->OnLightsChannelOpen(GetJSONSender());
    } else if (state == webrtc::DataChannelInterface::kClosed) {
      observer()->OnLightsChannelClosed();
    }
  }
};

class LocationChannelHandler : public DataChannelHandler {
 public:
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnLocationMessage(msg.data.cdata(), msg.size());
    return {};
  }
  void OnFirstMessage() override {
    // Notify location channel opening when actually using the channel,
    // it has the same reason with AdbChannelHandler::OnMessageInner,
    // to avoid unnecessary connections.
    observer()->OnLocationChannelOpen(GetBinarySender());
  }
};

class KmlLocationChannelHandler : public DataChannelHandler {
 public:
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnKmlLocationsMessage(msg.data.cdata(), msg.size());
    return {};
  }
  void OnFirstMessage() override {
    // Notify location channel opening when actually using the channel,
    // it has the same reason with AdbChannelHandler::OnMessageInner,
    // to avoid unnecessary connections.
    observer()->OnKmlLocationsChannelOpen(GetBinarySender());
  }
};

class GpxLocationChannelHandler : public DataChannelHandler {
 public:
  Result<void> OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnGpxLocationsMessage(msg.data.cdata(), msg.size());
    return {};
  }
  void OnFirstMessage() override {
    // Notify location channel opening when actually using the channel,
    // it has the same reason with AdbChannelHandler::OnMessageInner,
    // to avoid unnecessary connections.
    observer()->OnGpxLocationsChannelOpen(GetBinarySender());
  }
};

class UnknownChannelHandler : public DataChannelHandler {
 public:
  Result<void> OnMessageInner(const webrtc::DataBuffer &) override {
    LOG(WARNING) << "Message received on unknown channel: "
                 << channel()->label();
    return {};
  }
};

template <typename H>
class DataChannelHandlerImpl : public H {
 public:
  DataChannelHandlerImpl(
      rtc::scoped_refptr<webrtc::DataChannelInterface> channel,
      std::shared_ptr<ConnectionObserver> observer)
      : channel_(channel), observer_(observer) {
    channel->RegisterObserver(this);
  }
  ~DataChannelHandlerImpl() override { channel_->UnregisterObserver(); }

 protected:
  // DataChannelHandler implementation
  rtc::scoped_refptr<webrtc::DataChannelInterface> channel() override {
    return channel_;
  }
  std::shared_ptr<ConnectionObserver> observer() override { return observer_; }

 private:
  rtc::scoped_refptr<webrtc::DataChannelInterface> channel_;
  std::shared_ptr<ConnectionObserver> observer_;
};

}  // namespace

bool DataChannelHandler::Send(const uint8_t *msg, size_t size, bool binary) {
  webrtc::DataBuffer buffer(rtc::CopyOnWriteBuffer(msg, size), binary);
  // TODO (b/185832105): When the SCTP channel is congested data channel
  // messages are buffered up to 16MB, when the buffer is full the channel
  // is abruptly closed. Keep track of the buffered data to avoid losing the
  // adb data channel.
  return channel()->Send(buffer);
}

bool DataChannelHandler::Send(const Json::Value &message) {
  Json::StreamWriterBuilder factory;
  std::string message_string = Json::writeString(factory, message);
  return Send(reinterpret_cast<const uint8_t *>(message_string.c_str()),
              message_string.size(), /*binary=*/false);
}

void DataChannelHandler::OnStateChange() {
  LOG(VERBOSE) << channel()->label() << " channel state changed to "
               << webrtc::DataChannelInterface::DataStateString(
                      channel()->state());
  OnStateChangeInner(channel()->state());
}

void DataChannelHandler::OnMessage(const webrtc::DataBuffer &msg) {
  if (!first_msg_received_) {
    first_msg_received_ = true;
    OnFirstMessage();
  }
  auto res = OnMessageInner(msg);
  if (!res.ok()) {
    LOG(ERROR) << res.error().FormatForEnv();
  }
}

DataChannelHandlers::DataChannelHandlers(
    std::shared_ptr<ConnectionObserver> observer)
    : observer_(observer) {}

DataChannelHandlers::~DataChannelHandlers() {}

void DataChannelHandlers::OnDataChannelOpen(
    rtc::scoped_refptr<webrtc::DataChannelInterface> channel) {
  auto label = channel->label();
  LOG(VERBOSE) << "Data channel connected: " << label;
  if (label == kInputChannelLabel) {
    input_.reset(
        new DataChannelHandlerImpl<InputChannelHandler>(channel, observer_));
  } else if (label == kControlChannelLabel) {
    control_.reset(
        new DataChannelHandlerImpl<ControlChannelHandler>(channel, observer_));
  } else if (label == kAdbChannelLabel) {
    adb_.reset(
        new DataChannelHandlerImpl<AdbChannelHandler>(channel, observer_));
  } else if (label == kBluetoothChannelLabel) {
    bluetooth_.reset(new DataChannelHandlerImpl<BluetoothChannelHandler>(
        channel, observer_));
  } else if (label == kCameraDataChannelLabel) {
    camera_.reset(
        new DataChannelHandlerImpl<CameraChannelHandler>(channel, observer_));
  } else if (label == kLightsChannelLabel) {
    lights_.reset(
        new DataChannelHandlerImpl<LightsChannelHandler>(channel, observer_));
  } else if (label == kLocationDataChannelLabel) {
    location_.reset(
        new DataChannelHandlerImpl<LocationChannelHandler>(channel, observer_));
  } else if (label == kKmlLocationsDataChannelLabel) {
    kml_location_.reset(new DataChannelHandlerImpl<KmlLocationChannelHandler>(
        channel, observer_));
  } else if (label == kGpxLocationsDataChannelLabel) {
    gpx_location_.reset(new DataChannelHandlerImpl<GpxLocationChannelHandler>(
        channel, observer_));
  } else if (label == kSensorsDataChannelLabel) {
    sensors_.reset(
        new DataChannelHandlerImpl<SensorsChannelHandler>(channel, observer_));
  } else {
    unknown_channels_.emplace_back(
        new DataChannelHandlerImpl<UnknownChannelHandler>(channel, observer_));
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
