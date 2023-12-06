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

LightsObserver::LightsObserver(unsigned int port, unsigned int cid,
                               bool vhost_user_vsock)
    : cid_(cid),
      port_(port),
      vhost_user_vsock_(vhost_user_vsock),
      is_running_(false),
      session_active_(false),
      last_client_channel_id_(-1) {}

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
      if (is_running_ &&
          !cvd_connection_.Connect(
              port_, cid_,
              vhost_user_vsock_
                  ? std::optional(0) /* any value is okay for client */
                  : std::nullopt)) {
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
  } else if (json_value[kEventKey] == kMessageUpdate && session_active_) {
    // Cache the latest update for when new clients register
    std::lock_guard<std::mutex> lock(clients_lock_);
    cached_latest_update_ = json_value;

    // Send update to all subscribed clients
    for (auto itr = client_message_senders_.begin();
         itr != client_message_senders_.end(); itr++) {
      itr->second(json_value);
    }
  }
}

int LightsObserver::Subscribe(
    std::function<bool(const Json::Value&)> lights_message_sender) {
  int client_id = -1;
  {
    std::lock_guard<std::mutex> lock(clients_lock_);
    client_id = ++last_client_channel_id_;
    client_message_senders_[client_id] = lights_message_sender;

    if (!cached_latest_update_.empty()) {
      lights_message_sender(cached_latest_update_);
    }
  }

  return client_id;
}

void LightsObserver::Unsubscribe(int id) {
  std::lock_guard<std::mutex> lock(clients_lock_);
  client_message_senders_.erase(id);
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
