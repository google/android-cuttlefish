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

#include "host/frontend/webrtc_operator/device_list_handler.h"

namespace cuttlefish {

DeviceListHandler::DeviceListHandler(struct lws* wsi,
                                           DeviceRegistry& registry)
    : DynHandler(wsi), registry_(registry) {}

HttpStatusCode DeviceListHandler::DoGet() {
  Json::Value reply(Json::ValueType::arrayValue);

  for (const auto& id : registry_.ListDeviceIds()) {
    reply.append(id);
  }
  Json::StreamWriterBuilder json_factory;
  auto replyAsString = Json::writeString(json_factory, reply);
  AppendDataOut(replyAsString);
  return HttpStatusCode::Ok;
}
HttpStatusCode DeviceListHandler::DoPost() {
  return HttpStatusCode::NotFound;
}

}  // namespace cuttlefish
