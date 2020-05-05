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

#include "host/frontend/gcastv2/signaling_server/server_config.h"

#include <android-base/strings.h>

using android::base::StartsWith;

namespace cvd {

namespace {
  constexpr auto kStunPrefix = "stun:";
}

ServerConfig::ServerConfig(const std::vector<std::string>& stuns)
    : stun_servers_(stuns) {}

Json::Value ServerConfig::ToJson() const {
  Json::Value ice_servers(Json::ValueType::arrayValue);
  for (const auto& str : stun_servers_) {
    Json::Value server;
    server["urls"] = StartsWith(str, kStunPrefix)? str: kStunPrefix + str;
    ice_servers.append(server);
  }
  Json::Value server_config;
  server_config["ice_servers"] = ice_servers;
  return server_config;
}

}  // namespace cvd
