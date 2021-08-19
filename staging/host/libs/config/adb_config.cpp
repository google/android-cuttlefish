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
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <algorithm>
#include <set>

#include "common/libs/utils/environment.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/config_fragment.h"

namespace cuttlefish {

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

class AdbConfigImpl : public AdbConfig {
 public:
  INJECT(AdbConfigImpl(ConfigFlag& config_flag)) : config_flag_(config_flag) {
    Flag run_adb_connector =
        GflagsCompatFlag("run_adb_connector", run_adb_connector_);
    run_adb_connector.Help(
        "Maintain adb connection by sending 'adb connect' commands to the "
        "server. Only relevant with -adb_mode=tunnel or vsock_tunnel.");
    Flag adb_mode = GflagsCompatFlag("adb_mode");
    adb_mode.Help(
        "Mode for ADB connection."
        "'vsock_tunnel' for a TCP connection tunneled through vsock, "
        "'native_vsock' for a  direct connection to the guest ADB over "
        "vsock, 'vsock_half_tunnel' for a TCP connection forwarded to "
        "the guest ADB server, or a comma separated list of types as in "
        "'native_vsock,vsock_half_tunnel'");
    adb_mode.Getter([this]() {
      std::stringstream modes;
      for (const auto& mode : adb_mode_) {
        modes << "," << AdbModeToString(mode);
      }
      return modes.str().substr(1);  // First comma
    });
    adb_mode.Setter([this](const FlagMatch& match) {
      // TODO(schuffelen): Error on unknown types?
      adb_mode_.clear();
      for (auto& mode : android::base::Split(match.value, ",")) {
        adb_mode_.insert(StringToAdbMode(mode));
      }
      return true;
    });
    flags_ = {run_adb_connector, adb_mode};
  }

  std::set<AdbMode> adb_mode() const override { return adb_mode_; }
  bool run_adb_connector() const override { return run_adb_connector_; }

  std::string Name() const override { return "AdbConfig"; }

  Json::Value Serialize() const override {
    Json::Value json;
    json[kMode] = Json::Value(Json::arrayValue);
    for (const auto& mode : adb_mode_) {
      json[kMode].append(AdbModeToString(mode));
    }
    json[kConnectorEnabled] = run_adb_connector_;
    return json;
  }
  bool Deserialize(const Json::Value& json) override {
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

  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_flag_)};
  }

 protected:
  bool Process(std::vector<std::string>& args) override {
    // Defaults
    run_adb_connector_ = !IsRunningInContainer();
    adb_mode_ = {AdbMode::VsockHalfTunnel};
    bool success = ParseFlags(flags_, args);

    auto adb_modes_check = adb_mode_;
    adb_modes_check.erase(AdbMode::Unknown);
    if (adb_modes_check.size() < 1) {
      LOG(INFO) << "ADB not enabled";
    }

    return success;
  }
  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    return WriteGflagsCompatXml(flags_, out);
  }

 private:
  static constexpr char kMode[] = "mode";
  static constexpr char kConnectorEnabled[] = "connector_enabled";

  ConfigFlag& config_flag_;
  std::set<AdbMode> adb_mode_;
  bool run_adb_connector_;
  std::vector<Flag> flags_;
};

fruit::Component<fruit::Required<ConfigFlag>, AdbConfig> AdbConfigComponent() {
  return fruit::createComponent()
      .bind<AdbConfig, AdbConfigImpl>()
      .addMultibinding<ConfigFragment, AdbConfig>()
      .addMultibinding<FlagFeature, AdbConfig>();
}

}  // namespace cuttlefish
