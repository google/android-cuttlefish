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

#pragma once
#include "common/libs/utils/vsock_connection.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <json/json.h>

namespace cuttlefish {
namespace webrtc_streaming {

struct Light {
  enum class Type {
    BACKLIGHT = 0,
    KEYBOARD,
    BUTTONS,
    BATTERY,
    NOTIFICATIONS,
    ATTENTION,
    BLUETOOTH,
    WIFI,
    MICROPHONE,
    CAMERA,
  };

  unsigned int id;
  unsigned int color;
  Type light_type;
};

class LightsObserver {
 public:
  LightsObserver(unsigned int port, unsigned int cid, bool vhost_user_vsock);
  ~LightsObserver();

  LightsObserver(const LightsObserver& other) = delete;
  LightsObserver& operator=(const LightsObserver& other) = delete;

  bool Start();
  int Subscribe(std::function<bool(const Json::Value&)> lights_message_sender);
  void Unsubscribe(int lights_message_sender_id);

 private:
  void Stop();
  void ReadServerMessages();
  // TODO(b/295543722): Move to a virtio_console transport instead.
  VsockClientConnection cvd_connection_;
  unsigned int cid_;
  unsigned int port_;
  bool vhost_user_vsock_;
  std::thread connection_thread_;
  std::atomic<bool> is_running_;
  std::atomic<bool> session_active_;
  std::unordered_map<unsigned int, Light> lights_state_;

  std::mutex clients_lock_;
  Json::Value cached_latest_update_;
  std::unordered_map<int, std::function<bool(const Json::Value&)>>
      client_message_senders_;
  int last_client_channel_id_;
};

}  // namespace webrtc_streaming
}  // namespace cuttlefish
