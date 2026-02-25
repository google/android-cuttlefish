/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "cuttlefish/host/libs/config/config_flag.h"

#include <stddef.h>

#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <fmt/ranges.h>
#include <fruit/component.h>
#include <fruit/fruit_forward_decls.h>
#include <fruit/macro.h>
#include <gflags/gflags.h>
#include <json/value.h>
#include <json/writer.h>
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/key_equals_value/key_equals_value.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/config_utils.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/feature/feature.h"
#include "cuttlefish/result/result.h"

using android::base::ReadFileToString;
using gflags::FlagSettingMode::SET_FLAGS_DEFAULT;

namespace cuttlefish {

namespace {

constexpr auto kDefaultConfig = "phone";

std::string VectorizedFlagValue(const std::vector<std::string>& value) {
  return absl::StrJoin(value, ",");
}

class ConfigReader : public FlagFeature {
 public:
  INJECT(ConfigReader()) = default;

  bool HasConfig(const std::string& name) const {
    return allowed_config_presets_.count(name) > 0;
  }
  const std::set<std::string>& AvailableConfigs() const {
    return allowed_config_presets_;
  }
  Result<Json::Value> ReadConfig(const std::string& name) const {
    auto path =
        DefaultHostArtifactsPath("etc/cvd_config/cvd_config_" + name + ".json");
    std::string config_contents;
    CF_EXPECTF(
        ReadFileToString(path, &config_contents, /* follow_symlinks */ true),
        "Could not read config file \"{}\"", path);
    return CF_EXPECTF(ParseJson(config_contents),
                      "Could not parse config file \"{}\"", path);
  }

  // FlagFeature
  std::string Name() const override { return "ConfigReader"; }
  std::unordered_set<FlagFeature*> Dependencies() const override { return {}; }
  Result<void> Process(std::vector<std::string>&) override {
    auto config_path = DefaultHostArtifactsPath("etc/cvd_config");
    auto dir_contents = CF_EXPECT(DirectoryContents(config_path));
    for (const std::string& file : dir_contents) {
      std::string_view local_file(file);
      if (android::base::ConsumePrefix(&local_file, "cvd_config_") &&
          android::base::ConsumeSuffix(&local_file, ".json")) {
        allowed_config_presets_.emplace(local_file);
      }
    }
    return {};
  }
  bool WriteGflagsCompatHelpXml(std::ostream&) const override { return true; }

 private:
  std::set<std::string> allowed_config_presets_;
};

class ConfigFlagImpl : public ConfigFlag {
 public:
  INJECT(ConfigFlagImpl(ConfigReader& cr, SystemImageDirFlag& s))
      : config_reader_(cr),
        system_image_dir_flag_(s),
        configs_(s.Size(), kDefaultConfig),
        is_default_(true) {
    auto help =
        "Config preset name. Will automatically set flag fields using the "
        "values from this file of presets. See "
        "device/google/cuttlefish/shared/config/config_*.json for possible "
        "values.";
    auto getter = [this]() { return VectorizedFlagValue(configs_); };
    auto setter = [this](const FlagMatch& m) -> Result<void> {
      CF_EXPECT(ChooseConfigs(m.value));
      return {};
    };
    flag_ = GflagsCompatFlag("config").Help(help).Getter(getter).Setter(setter);
  }

  std::string Name() const override { return "ConfigFlagImpl"; }
  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {
        static_cast<FlagFeature*>(&config_reader_),
    };
  }
  Result<void> Process(std::vector<std::string>& args) override {
    CF_EXPECT(flag_.Parse(args), "Failed to parse `--config` flag");

    if (is_default_) {
      configs_.resize(system_image_dir_flag_.Size());
      // The default value is read from android_info.txt when available
      for (size_t i = 0; i < configs_.size(); ++i) {
        std::optional<std::string> info_cfg = FindAndroidInfoConfig(i);
        configs_[i] = info_cfg.value_or(kDefaultConfig);
      }
    }
    std::map<std::string, std::vector<std::string>> flags;
    LOG(INFO) << "Launching CVD using --config='"
              << VectorizedFlagValue(configs_) << "'.";
    for (size_t i = 0; i < configs_.size(); ++i) {
      Json::Value config_values =
          CF_EXPECT(config_reader_.ReadConfig(configs_[i]));
      for (const std::string& flag : config_values.getMemberNames()) {
        std::string value;
        if (flag == "custom_actions") {
          Json::StreamWriterBuilder factory;
          value = Json::writeString(factory, config_values[flag]);
        } else {
          value = config_values[flag].asString();
        }
        flags[flag].push_back(value);
      }
    }
    for (const auto& [flag, values] : flags) {
      auto value = VectorizedFlagValue(values);
      args.insert(args.begin(), "--" + flag + "=" + value);
      // To avoid the flag forwarder from thinking this song is different from a
      // default. Should fail silently if the flag doesn't exist.
      gflags::SetCommandLineOptionWithMode(flag.c_str(), value.c_str(),
                                           SET_FLAGS_DEFAULT);
    }
    return {};
  }
  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    return flag_.WriteGflagsCompatXml(out);
  }

 private:
  Result<void> ChooseConfigs(const std::string& value) {
    std::vector<std::string> configs = absl::StrSplit(value, ",");
    for (const auto& name : configs) {
      CF_EXPECTF(config_reader_.HasConfig(name),
                 "Invalid --config option '{}'. Valid options: [{}]", name,
                 fmt::join(config_reader_.AvailableConfigs(), ","));
    }
    configs_ = configs;
    is_default_ = false;
    return {};
  }
  std::optional<std::string> FindAndroidInfoConfig(size_t index) const {
    std::string info_path =
        system_image_dir_flag_.ForIndex(index) + "/android-info.txt";

    LOG(INFO) << "Reading --config option from: " << info_path;
    if (!FileExists(info_path)) {
      return {};
    }
    std::string android_info;
    if (!ReadFileToString(info_path, &android_info,
                          /* follow_symlinks */ true)) {
      return {};
    }
    Result<std::map<std::string, std::string, std::less<void>>> parsed_config =
        ParseKeyEqualsValue(android_info);
    if (!parsed_config.ok()) {
      return {};
    }
    auto config_it = parsed_config->find("config");
    if (config_it == parsed_config->end()) {
      return {};
    }
    if (!config_reader_.HasConfig(config_it->second)) {
      LOG(WARNING) << info_path << " contains invalid config preset: '"
                   << config_it->second << "'.";
      return {};
    }
    return config_it->second;
  }

  ConfigReader& config_reader_;
  SystemImageDirFlag& system_image_dir_flag_;
  std::vector<std::string> configs_;
  bool is_default_;
  Flag flag_;
};

class ConfigFlagPlaceholderImpl : public ConfigFlag {
 public:
  INJECT(ConfigFlagPlaceholderImpl()) {}

  std::string Name() const override { return "ConfigFlagPlaceholderImpl"; }
  std::unordered_set<FlagFeature*> Dependencies() const override { return {}; }
  Result<void> Process(std::vector<std::string>&) override { return {}; }
  bool WriteGflagsCompatHelpXml(std::ostream&) const override { return true; }
};

}  // namespace

fruit::Component<fruit::Required<SystemImageDirFlag>, ConfigFlag>
ConfigFlagComponent() {
  return fruit::createComponent()
      .addMultibinding<FlagFeature, ConfigReader>()
      .bind<ConfigFlag, ConfigFlagImpl>()
      .addMultibinding<FlagFeature, ConfigFlag>();
}

fruit::Component<ConfigFlag> ConfigFlagPlaceholder() {
  return fruit::createComponent()
      .addMultibinding<FlagFeature, ConfigFlag>()
      .bind<ConfigFlag, ConfigFlagPlaceholderImpl>();
}

}  // namespace cuttlefish
