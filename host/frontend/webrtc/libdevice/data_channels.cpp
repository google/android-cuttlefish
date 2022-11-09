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

#include "host/frontend/webrtc/libcommon/utils.h"
#include "host/frontend/webrtc/libdevice/keyboard.h"

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
  virtual void OnMessageInner(const webrtc::DataBuffer &msg) = 0;
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
static constexpr auto kLocationDataChannelLabel = "location-channel";
static constexpr auto kKmlLocationsDataChannelLabel = "kml-locations-channel";
static constexpr auto kGpxLocationsDataChannelLabel = "gpx-locations-channel";
static constexpr auto kCameraDataEof = "EOF";

// These classes use the Template pattern to minimize code repetition between
// data channel handlers.

class InputChannelHandler : public DataChannelHandler {
 public:
  void OnMessageInner(const webrtc::DataBuffer &msg) override {
    if (msg.binary) {
      // TODO (jemoreira) consider binary protocol to avoid JSON parsing
      // overhead
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

      observer()->OnTouchEvent(label, x, y, down);
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

      observer()->OnMultiTouchEvent(label, idArr, slotArr, xArr, yArr, down,
                                    size);
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
      observer()->OnKeyboardEvent(code, down);
    } else {
      LOG(ERROR) << "Unrecognized event type: " << event_type;
      return;
    }
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
  void OnMessageInner(const webrtc::DataBuffer &msg) override {
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
        observer()->OnLidStateChange(evt["lid_switch_open"].asBool());
      }
      if (evt.isMember("hinge_angle_value")) {
        observer()->OnHingeAngleChange(evt["hinge_angle_value"].asInt());
      }
      return;
    } else if (command.rfind("camera_", 0) == 0) {
      observer()->OnCameraControlMsg(evt);
      return;
    }

    auto button_state = evt["button_state"].asString();
    LOG(VERBOSE) << "Control command: " << command << " (" << button_state
                 << ")";
    if (command == "power") {
      observer()->OnPowerButton(button_state == "down");
    } else if (command == "back") {
      observer()->OnBackButton(button_state == "down");
    } else if (command == "home") {
      observer()->OnHomeButton(button_state == "down");
    } else if (command == "menu") {
      observer()->OnMenuButton(button_state == "down");
    } else if (command == "volumedown") {
      observer()->OnVolumeDownButton(button_state == "down");
    } else if (command == "volumeup") {
      observer()->OnVolumeUpButton(button_state == "down");
    } else {
      observer()->OnCustomActionButton(command, button_state);
    }
  }
};

class AdbChannelHandler : public DataChannelHandler {
 public:
  void OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnAdbMessage(msg.data.cdata(), msg.size());
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
  void OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnBluetoothMessage(msg.data.cdata(), msg.size());
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
  void OnMessageInner(const webrtc::DataBuffer &msg) override {
    auto msg_data = msg.data.cdata<char>();
    if (msg.size() == strlen(kCameraDataEof) &&
        !strncmp(msg_data, kCameraDataEof, msg.size())) {
      // Send complete buffer to observer on EOF marker
      observer()->OnCameraData(receive_buffer_);
      receive_buffer_.clear();
      return;
    }
    // Otherwise buffer up data
    receive_buffer_.insert(receive_buffer_.end(), msg_data,
                           msg_data + msg.size());
  }

 private:
  std::vector<char> receive_buffer_;
};

class LocationChannelHandler : public DataChannelHandler {
 public:
  void OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnLocationMessage(msg.data.cdata(), msg.size());
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
  void OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnKmlLocationsMessage(msg.data.cdata(), msg.size());
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
  void OnMessageInner(const webrtc::DataBuffer &msg) override {
    observer()->OnGpxLocationsMessage(msg.data.cdata(), msg.size());
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
  void OnMessageInner(const webrtc::DataBuffer &) override {
    LOG(WARNING) << "Message received on unknown channel: "
                 << channel()->label();
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
  OnMessageInner(msg);
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
  } else if (label == kLocationDataChannelLabel) {
    location_.reset(
        new DataChannelHandlerImpl<LocationChannelHandler>(channel, observer_));
  } else if (label == kKmlLocationsDataChannelLabel) {
    kml_location_.reset(new DataChannelHandlerImpl<KmlLocationChannelHandler>(
        channel, observer_));
  } else if (label == kGpxLocationsDataChannelLabel) {
    gpx_location_.reset(new DataChannelHandlerImpl<GpxLocationChannelHandler>(
        channel, observer_));
  } else {
    unknown_channels_.emplace_back(
        new DataChannelHandlerImpl<UnknownChannelHandler>(channel, observer_));
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
