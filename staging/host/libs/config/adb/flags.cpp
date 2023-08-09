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
#include "host/libs/config/adb/adb.h"

#include <android-base/strings.h>

#include "common/libs/utils/flag_parser.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/feature.h"

namespace cuttlefish {
namespace {

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

  Result<void> Process(std::vector<std::string>& args) override {
    // Defaults
    config_.SetModes({AdbMode::VsockHalfTunnel});
    bool run_adb_connector = !IsRunningInContainer();
    Flag run_flag = GflagsCompatFlag("run_adb_connector", run_adb_connector);
    CF_EXPECT(ParseFlags({run_flag, mode_flag_}, args),
              "Failed to parse adb config flags");
    config_.SetRunConnector(run_adb_connector);

    auto adb_modes_check = config_.Modes();
    adb_modes_check.erase(AdbMode::Unknown);
    if (adb_modes_check.size() < 1) {
      LOG(INFO) << "ADB not enabled";
    }

    return {};
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

fruit::Component<fruit::Required<AdbConfig, ConfigFlag>, AdbConfigFlag>
AdbConfigFlagComponent() {
  return fruit::createComponent()
      .bind<AdbConfigFlag, AdbConfigFlagImpl>()
      .addMultibinding<FlagFeature, AdbConfigFlag>();
}

}  // namespace cuttlefish
