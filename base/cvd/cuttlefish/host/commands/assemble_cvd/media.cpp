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

#include "cuttlefish/host/commands/assemble_cvd/media.h"

#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/host/libs/config/media.h"

namespace cuttlefish {
namespace {

class MediaConfigsImpl : public MediaConfigs {
 public:
  INJECT(MediaConfigsImpl()) {}

  const std::vector<CuttlefishConfig::MediaConfig>& GetConfigs()
      const override {
    return media_configs_;
  }

  void SetConfigs(const std::vector<CuttlefishConfig::MediaConfig>& configs) override {
    media_configs_ = configs;
  }

  std::string Name() const override { return "MediaConfigsImpl"; }

 private:
  std::vector<CuttlefishConfig::MediaConfig> media_configs_;
};

}  // namespace

fruit::Component<MediaConfigs> MediaConfigsComponent() {
  return fruit::createComponent()
      .bind<MediaConfigs, MediaConfigsImpl>()
      .addMultibinding<MediaConfigs, MediaConfigs>();
}

namespace {

class MediaConfigsFlagImpl : public MediaConfigsFlag {
 public:
  INJECT(MediaConfigsFlagImpl(MediaConfigs& configs,
                                ConfigFlag& config_flag))
      : media_configs_(configs), config_flag_dependency_(config_flag) {}

  std::string Name() const override { return "MediaConfigsFlagImpl"; }

  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_flag_dependency_)};
  }

  Result<void> Process(std::vector<std::string>& args) override {
    media_configs_.SetConfigs(CF_EXPECT(ParseMediaConfigsFromArgs(args)));
    return {};
  }

  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    Flag media_flag = GflagsCompatFlag(kMediaFlag).Help(kMediaHelp);
    return WriteGflagsCompatXml({media_flag}, out);
  }

 private:
  MediaConfigs& media_configs_;
  ConfigFlag& config_flag_dependency_;
};

}  // namespace

fruit::Component<fruit::Required<MediaConfigs, ConfigFlag>,
                 MediaConfigsFlag>
MediaConfigsFlagComponent() {
  return fruit::createComponent()
      .bind<MediaConfigsFlag, MediaConfigsFlagImpl>()
      .addMultibinding<FlagFeature, MediaConfigsFlag>();
}

namespace {

class MediaConfigsFragmentImpl : public MediaConfigsFragment {
 public:
  INJECT(MediaConfigsFragmentImpl(MediaConfigs& configs))
      : configs_(configs) {}

  std::string Name() const override { return "MediaConfigsFragmentImpl"; }

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
    if (!json.isMember(kMediaConfigs)) {
      LOG(ERROR) << "Invalid value for " << kMediaConfigs;
      return false;
    }

    const Json::Value& configs_json = json[kMediaConfigs];

    std::vector<CuttlefishConfig::MediaConfig> configs;
    for (auto& json : configs_json) {
      CuttlefishConfig::MediaConfig config = {};
      config.type =
          static_cast<CuttlefishConfig::MediaType>(json[kType].asInt());
      configs.emplace_back(config);
    }

    configs_.SetConfigs(configs);
    return true;
  }

 private:
  static constexpr char kMediaConfigs[] = "media_configs";
  static constexpr char kType[] = "type";
  MediaConfigs& configs_;
};

}  // namespace

fruit::Component<fruit::Required<MediaConfigs>, MediaConfigsFragment>
MediaConfigsFragmentComponent() {
  return fruit::createComponent()
      .bind<MediaConfigsFragment, MediaConfigsFragmentImpl>()
      .addMultibinding<ConfigFragment, MediaConfigsFragment>();
}

}  // namespace cuttlefish

