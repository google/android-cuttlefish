/*
 * Copyright (C) 2018 The Android Open Source Project
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
#include "host/libs/config/adb_config.h"

#include <android-base/logging.h>
#include <fruit/fruit.h>
#include <algorithm>
#include <set>

#include "host/libs/config/config_fragment.h"

namespace cuttlefish {

AdbConfig::AdbConfig() = default;

static AdbMode StringToAdbMode(std::string mode) {
  std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
  if (mode == "vsock_tunnel") {
    return AdbMode::VsockTunnel;
  } else if (mode == "vsock_half_tunnel") {
    return AdbMode::VsockHalfTunnel;
  } else if (mode == "native_vsock") {
    return AdbMode::NativeVsock;
  } else {
    return AdbMode::Unknown;
  }
}

static std::string AdbModeToString(AdbMode mode) {
  switch (mode) {
    case AdbMode::VsockTunnel:
      return "vsock_tunnel";
    case AdbMode::VsockHalfTunnel:
      return "vsock_half_tunnel";
    case AdbMode::NativeVsock:
      return "native_vsock";
    case AdbMode::Unknown:  // fall through
    default:
      return "unknown";
  }
}

void AdbConfig::set_adb_mode(const std::set<std::string>& modes) {
  adb_mode_.clear();
  for (auto& mode : modes) {
    adb_mode_.insert(StringToAdbMode(mode));
  }
}
std::set<AdbMode> AdbConfig::adb_mode() const { return adb_mode_; }

void AdbConfig::set_run_adb_connector(bool run_adb_connector) {
  run_adb_connector_ = run_adb_connector;
}
bool AdbConfig::run_adb_connector() const { return run_adb_connector_; }

std::string AdbConfig::Name() const { return "adb"; }

constexpr char kMode[] = "mode";
constexpr char kConnectorEnabled[] = "connector_enabled";
Json::Value AdbConfig::Serialize() const {
  Json::Value json;
  json[kMode] = Json::Value(Json::arrayValue);
  for (const auto& mode : adb_mode_) {
    json[kMode].append(AdbModeToString(mode));
  }
  json[kConnectorEnabled] = run_adb_connector_;
  return json;
}
bool AdbConfig::Deserialize(const Json::Value& json) {
  if (!json.isMember(kMode) || json[kMode].type() != Json::arrayValue) {
    LOG(ERROR) << "Invalid value for " << kMode;
    return false;
  }
  adb_mode_.clear();
  for (auto& mode : json[kMode]) {
    if (mode.type() != Json::stringValue) {
      LOG(ERROR) << "Invalid mode type" << mode;
      return false;
    }
    adb_mode_.insert(StringToAdbMode(mode.asString()));
  }
  if (!json.isMember(kConnectorEnabled) ||
      json[kConnectorEnabled].type() != Json::booleanValue) {
    LOG(ERROR) << "Invalid value for " << kConnectorEnabled;
    return false;
  }
  run_adb_connector_ = json[kConnectorEnabled].asBool();
  return true;
}

fruit::Component<AdbConfig> AdbConfigComponent() {
  return fruit::createComponent().addMultibinding<ConfigFragment, AdbConfig>();
}

}  // namespace cuttlefish
