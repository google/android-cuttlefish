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

#include "host/frontend/gcastv2/signaling_server/device_list_handler.h"

namespace cuttlefish {

DeviceListHandler::DeviceListHandler(const DeviceRegistry& registry)
    : registry_(registry) {}

int DeviceListHandler::handleMessage(uint8_t /*header_byte*/,
                                     const uint8_t* /*msg*/,
                                     size_t /*len*/) {
  // ignore the message, just send the reply
  Json::Value reply(Json::ValueType::arrayValue);

  for (const auto& id : registry_.ListDeviceIds()) {
    reply.append(id);
  }
  Json::FastWriter json_writer;
  auto replyAsString = json_writer.write(reply);
  sendMessage(replyAsString.c_str(), replyAsString.size());
  return -1;  // disconnect
}

}  // namespace cuttlefish
