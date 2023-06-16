/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <memory>

#include "common/libs/fs/shared_fd.h"

#include "host/libs/input_connector/input_connector.h"

namespace cuttlefish{

enum class InputEventType {
  Virtio,
  Evdev,
};

class InputSocketsConnector;

class InputSocketsConnectorBuilder {
 public:
  InputSocketsConnectorBuilder(InputEventType type);
  ~InputSocketsConnectorBuilder();
  InputSocketsConnectorBuilder(const InputSocketsConnectorBuilder&) = delete;
  InputSocketsConnectorBuilder(InputSocketsConnectorBuilder&&) = delete;
  InputSocketsConnectorBuilder& operator=(const InputSocketsConnectorBuilder&) = delete;

  void WithTouchscreen(const std::string& display, SharedFD server);
  void WithKeyboard(SharedFD server);
  void WithSwitches(SharedFD server);
  void WithRotary(SharedFD server);
  // This object becomes invalid after calling Build(), the rvalue reference
  // makes it explicit that it shouldn't be used after.
  std::unique_ptr<InputConnector> Build() &&;

 private:
  std::unique_ptr<InputSocketsConnector> connector_;
};

}
