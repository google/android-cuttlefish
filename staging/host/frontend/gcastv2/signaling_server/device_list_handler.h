//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>
#include <string>

#include <json/json.h>

#include "host/frontend/gcastv2/https/include/https/WebSocketHandler.h"
#include "host/frontend/gcastv2/signaling_server/device_registry.h"

namespace cvd {

class DeviceListHandler : public WebSocketHandler {
 public:
  DeviceListHandler(const DeviceRegistry& registry);

 protected:
  int handleMessage(uint8_t, const uint8_t*, size_t) override;

 private:
  const DeviceRegistry& registry_;
};
}  // namespace cvd
