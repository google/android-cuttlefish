//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/camera.h"

#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/libs/config/camera.h"

namespace cuttlefish {
namespace {

class CamerasConfigsImpl : public CamerasConfigs {
 public:
  INJECT(CamerasConfigsImpl()) {}

  const std::vector<CuttlefishConfig::CameraConfig>& GetConfigs()
      const override {
    return camera_configs_;
  }

  void SetConfigs(const std::vector<CuttlefishConfig::CameraConfig>& configs) override {
    camera_configs_ = configs;
  }

  std::string Name() const override { return "CamerasConfigsImpl"; }

 private:
  std::vector<CuttlefishConfig::CameraConfig> camera_configs_;
};

}  // namespace

fruit::Component<CamerasConfigs> CamerasConfigsComponent() {
  return fruit::createComponent()
      .bind<CamerasConfigs, CamerasConfigsImpl>()
      .addMultibinding<CamerasConfigs, CamerasConfigs>();
}

namespace {

class CamerasConfigsFlagImpl : public CamerasConfigsFlag {
 public:
  INJECT(CamerasConfigsFlagImpl(CamerasConfigs& configs,
                                ConfigFlag& config_flag))
      : camera_configs_(configs), config_flag_dependency_(config_flag) {}

  std::string Name() const override { return "CamerasConfigsFlagImpl"; }

  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_flag_dependency_)};
  }

  Result<void> Process(std::vector<std::string>& args) override {
    camera_configs_.SetConfigs(CF_EXPECT(ParseCameraConfigsFromArgs(args)));
    return {};
  }

  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    Flag camera_flag = GflagsCompatFlag(kCameraFlag).Help(kCameraHelp);
    return WriteGflagsCompatXml({camera_flag}, out);
  }

 private:
  CamerasConfigs& camera_configs_;
  ConfigFlag& config_flag_dependency_;
};

}  // namespace

fruit::Component<fruit::Required<CamerasConfigs, ConfigFlag>,
                 CamerasConfigsFlag>
CamerasConfigsFlagComponent() {
  return fruit::createComponent()
      .bind<CamerasConfigsFlag, CamerasConfigsFlagImpl>()
      .addMultibinding<FlagFeature, CamerasConfigsFlag>();
}

namespace {

class CamerasConfigsFragmentImpl : public CamerasConfigsFragment {
 public:
  INJECT(CamerasConfigsFragmentImpl(CamerasConfigs& configs))
      : configs_(configs) {}

  std::string Name() const override { return "CamerasConfigsFragmentImpl"; }

  Json::Value Serialize() const override {
    Json::Value configs_json(Json::arrayValue);
    for (const auto& config : configs_.GetConfigs()) {
      Json::Value json(Json::objectValue);
      json[kType] = static_cast<int>(config.type);
      configs_json.append(json);
    }
    return configs_json;
  }

  bool Deserialize(const Json::Value& json) override {
    if (!json.isMember(kCameraConfigs)) {
      LOG(ERROR) << "Invalid value for " << kCameraConfigs;
      return false;
    }

    const Json::Value& configs_json = json[kCameraConfigs];

    std::vector<CuttlefishConfig::CameraConfig> configs;
    for (auto& json : configs_json) {
      CuttlefishConfig::CameraConfig config = {};
      config.type =
          static_cast<CuttlefishConfig::CameraType>(json[kType].asInt());
      configs.emplace_back(config);
    }

    configs_.SetConfigs(configs);
    return true;
  }

 private:
  static constexpr char kCameraConfigs[] = "camera_configs";
  static constexpr char kType[] = "type";
  CamerasConfigs& configs_;
};

}  // namespace

fruit::Component<fruit::Required<CamerasConfigs>, CamerasConfigsFragment>
CamerasConfigsFragmentComponent() {
  return fruit::createComponent()
      .bind<CamerasConfigsFragment, CamerasConfigsFragmentImpl>()
      .addMultibinding<ConfigFragment, CamerasConfigsFragment>();
}

}  // namespace cuttlefish

