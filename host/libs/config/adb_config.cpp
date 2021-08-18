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
namespace {

class AdbConfigImpl : public AdbConfig {
 public:
  INJECT(AdbConfigImpl()) {}

  const std::set<AdbMode>& Modes() const override { return modes_; }
  bool SetModes(const std::set<AdbMode>& modes) override {
    modes_ = modes;
    return true;
  }
  bool SetModes(std::set<AdbMode>&& modes) override {
    modes_ = std::move(modes);
    return true;
  }

  bool RunConnector() const override { return run_connector_; }
  bool SetRunConnector(bool run) override {
    run_connector_ = run;
    return true;
  }

 private:
  std::set<AdbMode> modes_;
  bool run_connector_;
};

AdbMode StringToAdbMode(std::string mode) {
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

std::string AdbModeToString(AdbMode mode) {
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

class AdbConfigFragmentImpl : public AdbConfigFragment {
 public:
  INJECT(AdbConfigFragmentImpl(AdbConfig& config)) : config_(config) {}

  std::string Name() const override { return "AdbConfigFragmentImpl"; }

  Json::Value Serialize() const override {
    Json::Value json;
    json[kMode] = Json::Value(Json::arrayValue);
    for (const auto& mode : config_.Modes()) {
      json[kMode].append(AdbModeToString(mode));
    }
    json[kConnectorEnabled] = config_.RunConnector();
    return json;
  }
  bool Deserialize(const Json::Value& json) override {
    if (!json.isMember(kMode) || json[kMode].type() != Json::arrayValue) {
      LOG(ERROR) << "Invalid value for " << kMode;
      return false;
    }
    std::set<AdbMode> modes;
    for (auto& mode : json[kMode]) {
      if (mode.type() != Json::stringValue) {
        LOG(ERROR) << "Invalid mode type" << mode;
        return false;
      }
      modes.insert(StringToAdbMode(mode.asString()));
    }
    if (!config_.SetModes(std::move(modes))) {
      LOG(ERROR) << "Failed to set adb modes";
      return false;
    }

    if (!json.isMember(kConnectorEnabled) ||
        json[kConnectorEnabled].type() != Json::booleanValue) {
      LOG(ERROR) << "Invalid value for " << kConnectorEnabled;
      return false;
    }
    if (!config_.SetRunConnector(json[kConnectorEnabled].asBool())) {
      LOG(ERROR) << "Failed to set whether to run the adb connector";
    }
    return true;
  }

 private:
  static constexpr char kMode[] = "mode";
  static constexpr char kConnectorEnabled[] = "connector_enabled";
  AdbConfig& config_;
};

class AdbConfigFlagImpl : public AdbConfigFlag {
 public:
  INJECT(AdbConfigFlagImpl(AdbConfig& config, ConfigFlag& config_flag))
      : config_(config), config_flag_(config_flag) {
    mode_flag_ = GflagsCompatFlag("adb_mode").Help(mode_help);
    mode_flag_.Getter([this]() {
      std::stringstream modes;
      for (const auto& mode : config_.Modes()) {
        modes << "," << AdbModeToString(mode);
      }
      return modes.str().substr(1);  // First comma
    });
    mode_flag_.Setter([this](const FlagMatch& match) {
      // TODO(schuffelen): Error on unknown types?
      std::set<AdbMode> modes;
      for (auto& mode : android::base::Split(match.value, ",")) {
        modes.insert(StringToAdbMode(mode));
      }
      return config_.SetModes(modes);
    });
  }

  std::string Name() const override { return "AdbConfigFlagImpl"; }

  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_flag_)};
  }

  bool Process(std::vector<std::string>& args) override {
    // Defaults
    config_.SetModes({AdbMode::VsockHalfTunnel});
    bool run_adb_connector = !IsRunningInContainer();
    Flag run_flag = GflagsCompatFlag("run_adb_connector", run_adb_connector);
    if (!ParseFlags({run_flag, mode_flag_}, args)) {
      LOG(ERROR) << "Failed to parse adb config flags";
      return false;
    }
    config_.SetRunConnector(run_adb_connector);

    auto adb_modes_check = config_.Modes();
    adb_modes_check.erase(AdbMode::Unknown);
    if (adb_modes_check.size() < 1) {
      LOG(INFO) << "ADB not enabled";
    }

    return true;
  }
  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    bool run = config_.RunConnector();
    Flag run_flag = GflagsCompatFlag("run_adb_connector", run).Help(run_help);
    return WriteGflagsCompatXml({run_flag, mode_flag_}, out);
  }

 private:
  static constexpr char run_help[] =
      "Maintain adb connection by sending 'adb connect' commands to the "
      "server. Only relevant with -adb_mode=tunnel or vsock_tunnel.";
  static constexpr char mode_help[] =
      "Mode for ADB connection."
      "'vsock_tunnel' for a TCP connection tunneled through vsock, "
      "'native_vsock' for a  direct connection to the guest ADB over "
      "vsock, 'vsock_half_tunnel' for a TCP connection forwarded to "
      "the guest ADB server, or a comma separated list of types as in "
      "'native_vsock,vsock_half_tunnel'";

  AdbConfig& config_;
  ConfigFlag& config_flag_;
  Flag mode_flag_;
};

}  // namespace

fruit::Component<AdbConfig> AdbConfigComponent() {
  return fruit::createComponent().bind<AdbConfig, AdbConfigImpl>();
}
fruit::Component<fruit::Required<AdbConfig, ConfigFlag>, AdbConfigFlag>
AdbConfigFlagComponent() {
  return fruit::createComponent()
      .bind<AdbConfigFlag, AdbConfigFlagImpl>()
      .addMultibinding<FlagFeature, AdbConfigFlag>();
}
fruit::Component<fruit::Required<AdbConfig>, AdbConfigFragment>
AdbConfigFragmentComponent() {
  return fruit::createComponent()
      .bind<AdbConfigFragment, AdbConfigFragmentImpl>()
      .addMultibinding<ConfigFragment, AdbConfigFragment>();
}

}  // namespace cuttlefish
