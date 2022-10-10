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

#include <json/json.h>

namespace cuttlefish {

class CameraController {
 public:
  virtual ~CameraController() = default;

  // Handle data messages coming from the client
  virtual void HandleMessage(const std::vector<char>& message) = 0;
  // Handle control messages coming from the client
  virtual void HandleMessage(const Json::Value& message) = 0;
  // Send control messages to client
  virtual void SendMessage(const Json::Value& msg) {
    if (message_sender_) {
      message_sender_(msg);
    }
  }
  virtual void SetMessageSender(
      std::function<bool(const Json::Value& msg)> sender) {
    message_sender_ = sender;
  }

 protected:
  std::function<bool(const Json::Value& msg)> message_sender_;
};

}  // namespace cuttlefish
