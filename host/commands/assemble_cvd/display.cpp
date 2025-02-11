//
// Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/assemble_cvd/display.h"

#include "common/libs/utils/flag_parser.h"
#include "host/libs/config/display.h"

namespace cuttlefish {
namespace {

class DisplaysConfigsImpl : public DisplaysConfigs {
 public:
  INJECT(DisplaysConfigsImpl()) {}

  const std::vector<CuttlefishConfig::DisplayConfig>& GetConfigs()
      const override {
    return display_configs_;
  }

  void SetConfigs(const std::vector<CuttlefishConfig::DisplayConfig>& configs) {
    display_configs_ = configs;
  }

  std::string Name() const override { return "DisplaysConfigsImpl"; }

 private:
  std::vector<CuttlefishConfig::DisplayConfig> display_configs_;
};

}  // namespace

fruit::Component<DisplaysConfigs> DisplaysConfigsComponent() {
  return fruit::createComponent()
      .bind<DisplaysConfigs, DisplaysConfigsImpl>()
      .addMultibinding<DisplaysConfigs, DisplaysConfigs>();
}

namespace {

class DisplaysConfigsFlagImpl : public DisplaysConfigsFlag {
 public:
  INJECT(DisplaysConfigsFlagImpl(DisplaysConfigs& configs,
                                 ConfigFlag& config_flag))
      : display_configs_(configs), config_flag_dependency_(config_flag) {}

  std::string Name() const override { return "DisplaysConfigsFlagImpl"; }

  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_flag_dependency_)};
  }

  Result<void> Process(std::vector<std::string>& args) override {
    display_configs_.SetConfigs(CF_EXPECT(ParseDisplayConfigsFromArgs(args)));
    return {};
  }

  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    Flag display_flag = GflagsCompatFlag(kDisplayFlag).Help(kDisplayHelp);
    return WriteGflagsCompatXml({display_flag}, out);
  }

 private:
  DisplaysConfigs& display_configs_;
  ConfigFlag& config_flag_dependency_;
};

}  // namespace

fruit::Component<fruit::Required<DisplaysConfigs, ConfigFlag>,
                 DisplaysConfigsFlag>
DisplaysConfigsFlagComponent() {
  return fruit::createComponent()
      .bind<DisplaysConfigsFlag, DisplaysConfigsFlagImpl>()
      .addMultibinding<FlagFeature, DisplaysConfigsFlag>();
}

namespace {

class DisplaysConfigsFragmentImpl : public DisplaysConfigsFragment {
 public:
  INJECT(DisplaysConfigsFragmentImpl(DisplaysConfigs& displays_configs))
      : displays_configs_(displays_configs) {}

  std::string Name() const override { return "DisplaysConfigsFragmentImpl"; }

  Json::Value Serialize() const override {
    Json::Value display_configs_json(Json::arrayValue);
    for (const auto& display_configs : displays_configs_.GetConfigs()) {
      Json::Value display_config_json(Json::objectValue);
      display_config_json[kXRes] = display_configs.width;
      display_config_json[kYRes] = display_configs.height;
      display_config_json[kDpi] = display_configs.dpi;
      display_config_json[kRefreshRateHz] = display_configs.refresh_rate_hz;
      display_configs_json.append(display_config_json);
    }
    return display_configs_json;
  }

  bool Deserialize(const Json::Value& json) override {
    if (!json.isMember(kDisplayConfigs)) {
      LOG(ERROR) << "Invalid value for " << kDisplayConfigs;
      return false;
    }

    const Json::Value& displays_configs_json = json[kDisplayConfigs];

    std::vector<CuttlefishConfig::DisplayConfig> displays_configs;
    for (auto& display_config_json : displays_configs_json) {
      CuttlefishConfig::DisplayConfig display_config = {};
      display_config.width = display_config_json[kXRes].asInt();
      display_config.height = display_config_json[kYRes].asInt();
      display_config.dpi = display_config_json[kDpi].asInt();
      display_config.refresh_rate_hz =
          display_config_json[kRefreshRateHz].asInt();
      displays_configs.emplace_back(display_config);
    }

    displays_configs_.SetConfigs(displays_configs);
    return true;
  }

 private:
  static constexpr char kDisplayConfigs[] = "display_configs";
  static constexpr char kXRes[] = "x_res";
  static constexpr char kYRes[] = "y_res";
  static constexpr char kDpi[] = "dpi";
  static constexpr char kRefreshRateHz[] = "refresh_rate_hz";

  DisplaysConfigs& displays_configs_;
};

}  // namespace

fruit::Component<fruit::Required<DisplaysConfigs>, DisplaysConfigsFragment>
DisplaysConfigsFragmentComponent() {
  return fruit::createComponent()
      .bind<DisplaysConfigsFragment, DisplaysConfigsFragmentImpl>()
      .addMultibinding<ConfigFragment, DisplaysConfigsFragment>();
}

}  // namespace cuttlefish
