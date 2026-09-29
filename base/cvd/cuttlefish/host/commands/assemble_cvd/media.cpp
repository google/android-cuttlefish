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

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "fruit/component.h"
#include "fruit/fruit_forward_decls.h"
#include "fruit/macro.h"
#include "json/value.h"

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
#include "cuttlefish/host/libs/config/config_flag.h"
#include "cuttlefish/host/libs/config/config_fragment.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/config/instance_nums.h"
#include "cuttlefish/host/libs/config/media.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class MediaConfigsImpl : public MediaConfigs {
 public:
  INJECT(MediaConfigsImpl()) {}

  const std::vector<CuttlefishConfig::MediaConfig>& GetConfigs(
      size_t instance_index) const override {
    if (instance_index < media_configs_.size()) {
      return media_configs_[instance_index];
    }
    static const std::vector<CuttlefishConfig::MediaConfig> kEmptyConfigs;
    return kEmptyConfigs;
  }

  const std::vector<std::vector<CuttlefishConfig::MediaConfig>>& GetAllConfigs()
      const override {
    return media_configs_;
  }

  void SetConfigs(const std::vector<std::vector<CuttlefishConfig::MediaConfig>>&
                      configs) override {
    media_configs_ = configs;
  }

  std::string Name() const override { return "MediaConfigsImpl"; }

 private:
  std::vector<std::vector<CuttlefishConfig::MediaConfig>> media_configs_;
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
  INJECT(MediaConfigsFlagImpl(MediaConfigs& configs, ConfigFlag& config_flag))
      : media_configs_(configs), config_flag_dependency_(config_flag) {}

  std::string Name() const override { return "MediaConfigsFlagImpl"; }

  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_flag_dependency_)};
  }

  Result<void> Process(std::vector<std::string>& args) override {
    const std::vector<int32_t> instance_nums =
        CF_EXPECT(InstanceNumsCalculator().FromFlags(args).Calculate());
    media_configs_.SetConfigs(
        CF_EXPECT(ParseMediaConfigsFromArgs(args, instance_nums.size())));
    return {};
  }

  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    Flag media_flag = Flag::StringFlag(kMediaFlag).Help(kMediaHelp);
    WriteGflagsCompatXml({media_flag}, out);
    return true;
  }

 private:
  MediaConfigs& media_configs_;
  ConfigFlag& config_flag_dependency_;
};

}  // namespace

fruit::Component<fruit::Required<MediaConfigs, ConfigFlag>, MediaConfigsFlag>
MediaConfigsFlagComponent() {
  return fruit::createComponent()
      .bind<MediaConfigsFlag, MediaConfigsFlagImpl>()
      .addMultibinding<FlagFeature, MediaConfigsFlag>();
}

namespace {

class MediaConfigsFragmentImpl : public MediaConfigsFragment {
 public:
  INJECT(MediaConfigsFragmentImpl(MediaConfigs& configs)) : configs_(configs) {}

  std::string Name() const override { return "MediaConfigsFragmentImpl"; }

  Json::Value Serialize() const override {
    Json::Value instances_json(Json::arrayValue);
    for (const std::vector<CuttlefishConfig::MediaConfig>& instance_configs :
         configs_.GetAllConfigs()) {
      Json::Value configs_json(Json::arrayValue);
      for (const CuttlefishConfig::MediaConfig& config : instance_configs) {
        Json::Value json(Json::objectValue);
        json[kType] = static_cast<int>(config.type);
        json[kLensFacing] = config.lens_facing;
        configs_json.append(json);
      }
      instances_json.append(configs_json);
    }
    Json::Value root(Json::objectValue);
    root[kMediaConfigs] = instances_json;
    return root;
  }

  bool Deserialize(const Json::Value& json) override {
    const Json::Value& configs_json =
        json.isMember(kMediaConfigs) ? json[kMediaConfigs] : json;
    if (!configs_json.isArray()) {
      LOG(ERROR) << "Invalid value for " << kMediaConfigs;
      return false;
    }

    std::vector<std::vector<CuttlefishConfig::MediaConfig>> all_configs;
    for (const auto& instance_json : configs_json) {
      if (!instance_json.isArray()) {
        LOG(ERROR) << "Invalid instance value for " << kMediaConfigs
                   << ", expected array of devices";
        return false;
      }
      std::vector<CuttlefishConfig::MediaConfig> instance_configs;
      for (const auto& item : instance_json) {
        CuttlefishConfig::MediaConfig config = {};
        config.type =
            static_cast<CuttlefishConfig::MediaType>(item[kType].asInt());
        if (item.isMember(kLensFacing)) {
          config.lens_facing = item[kLensFacing].asString();
        }
        instance_configs.emplace_back(config);
      }
      all_configs.emplace_back(std::move(instance_configs));
    }

    configs_.SetConfigs(all_configs);
    return true;
  }

 private:
  static constexpr char kMediaConfigs[] = "media_configs";
  static constexpr char kType[] = "type";
  static constexpr char kLensFacing[] = "lens_facing";
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
