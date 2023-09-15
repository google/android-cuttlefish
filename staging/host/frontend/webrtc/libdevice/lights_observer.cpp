/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include "lights_observer.h"

#include <android-base/logging.h>
#include <chrono>
#include "common/libs/utils/vsock_connection.h"

#include <json/json.h>

namespace cuttlefish {
namespace webrtc_streaming {

LightsObserver::LightsObserver(unsigned int port, unsigned int cid)
    : cid_(cid), port_(port), is_running_(false), session_active_(false) {}

LightsObserver::~LightsObserver() { Stop(); }

bool LightsObserver::Start() {
  if (connection_thread_.joinable()) {
    LOG(ERROR) << "Connection thread is already running.";
    return false;
  }

  is_running_ = true;

  connection_thread_ = std::thread([this] {
    while (is_running_) {
      while (cvd_connection_.IsConnected()) {
        ReadServerMessages();
      }

      // Try to start a new connection. If this fails, delay retrying a bit.
      if (is_running_ && !cvd_connection_.Connect(port_, cid_)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        continue;
      }
    }

    LOG(INFO) << "Exiting connection thread";
  });

  LOG(INFO) << "Connection thread running";
  return true;
}

void LightsObserver::Stop() {
  is_running_ = false;
  cvd_connection_.Disconnect();

  // The connection_thread_ should finish at any point now. Let's join it.
  if (connection_thread_.joinable()) {
    connection_thread_.join();
  }
}

void LightsObserver::ReadServerMessages() {
  static constexpr auto kEventKey = "event";
  static constexpr auto kMessageStart = "VIRTUAL_DEVICE_START_LIGHTS_SESSION";
  static constexpr auto kMessageStop = "VIRTUAL_DEVICE_STOP_LIGHTS_SESSION";
  static constexpr auto kMessageUpdate = "VIRTUAL_DEVICE_LIGHTS_UPDATE";

  auto json_value = cvd_connection_.ReadJsonMessage();

  if (json_value[kEventKey] == kMessageStart) {
    session_active_ = true;
  } else if (json_value[kEventKey] == kMessageStop) {
    session_active_ = false;
  } else if (json_value[kEventKey] == kMessageUpdate) {
    for (const auto& light : json_value["lights"]) {
      const unsigned int id = light["id"].asUInt();
      lights_state_[id] = Light{
          .id = id,
          .color = light["color"].asUInt(),
          .light_type = Light::Type(light["light_type"].asUInt()),
      };
    }
  }
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
