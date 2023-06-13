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

#include "host/libs/config/config_flag.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <json/json.h>
#include <fstream>
#include <set>
#include <string>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/json.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/libs/config/cuttlefish_config.h"

// To support other files that use this from gflags.
// TODO: Add a description to this flag
DEFINE_string(system_image_dir, CF_DEFAULTS_SYSTEM_IMAGE_DIR, "");

using gflags::FlagSettingMode::SET_FLAGS_DEFAULT;

namespace cuttlefish {

namespace {

class SystemImageDirFlagImpl : public SystemImageDirFlag {
 public:
  INJECT(SystemImageDirFlagImpl()) {
    auto help = "Location of the system partition images.";
    flag_ = GflagsCompatFlag("system_image_dir", path_).Help(help);
  }
  const std::string& Path() override { return path_; }

  std::string Name() const override { return "SystemImageDirFlagImpl"; }
  std::unordered_set<FlagFeature*> Dependencies() const override { return {}; }
  Result<void> Process(std::vector<std::string>& args) override {
    path_ = DefaultGuestImagePath("");
    CF_EXPECT(flag_.Parse(args));
    // To support other files that use this from gflags.
    FLAGS_system_image_dir = path_;
    gflags::SetCommandLineOptionWithMode("system_image_dir", path_.c_str(),
                                         SET_FLAGS_DEFAULT);
    return {};
  }
  bool WriteGflagsCompatHelpXml(std::ostream&) const override {
    // TODO(schuffelen): Write something here when this is removed from gflags
    return true;
  }

 private:
  std::string path_;
  Flag flag_;
};

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
    CF_EXPECTF(android::base::ReadFileToString(path, &config_contents),
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
      : config_reader_(cr), system_image_dir_flag_(s) {
    is_default_ = true;
    config_ = "phone";  // default value
    auto help =
        "Config preset name. Will automatically set flag fields using the "
        "values from this file of presets. See "
        "device/google/cuttlefish/shared/config/config_*.json for possible "
        "values.";
    auto getter = [this]() { return config_; };
    auto setter = [this](const FlagMatch& m) { return ChooseConfig(m.value); };
    flag_ = GflagsCompatFlag("config").Help(help).Getter(getter).Setter(setter);
  }

  std::string Name() const override { return "ConfigFlagImpl"; }
  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {
        static_cast<FlagFeature*>(&config_reader_),
        static_cast<FlagFeature*>(&system_image_dir_flag_),
    };
  }
  Result<void> Process(std::vector<std::string>& args) override {
    CF_EXPECT(flag_.Parse(args), "Failed to parse `--config` flag");

    if (auto info_cfg = FindAndroidInfoConfig(); is_default_ && info_cfg) {
      config_ = *info_cfg;
    }
    LOG(INFO) << "Launching CVD using --config='" << config_ << "'.";
    auto config_values = CF_EXPECT(config_reader_.ReadConfig(config_));
    for (const std::string& flag : config_values.getMemberNames()) {
      std::string value;
      if (flag == "custom_actions") {
        Json::StreamWriterBuilder factory;
        value = Json::writeString(factory, config_values[flag]);
      } else {
        value = config_values[flag].asString();
      }
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
  bool ChooseConfig(const std::string& name) {
    if (!config_reader_.HasConfig(name)) {
      LOG(ERROR) << "Invalid --config option '" << name << "'. Valid options: "
                 << android::base::Join(config_reader_.AvailableConfigs(), ",");
      return false;
    }
    config_ = name;
    is_default_ = false;
    return true;
  }
  std::optional<std::string> FindAndroidInfoConfig() const {
    auto info_path = system_image_dir_flag_.Path() + "/android-info.txt";
    if (!FileExists(info_path)) {
      return {};
    }
    std::ifstream ifs{info_path};
    if (!ifs.is_open()) {
      return {};
    }
    std::string android_info;
    ifs >> android_info;
    std::string_view local_android_info(android_info);
    if (!android::base::ConsumePrefix(&local_android_info, "config=")) {
      return {};
    }
    if (!config_reader_.HasConfig(std::string{local_android_info})) {
      LOG(WARNING) << info_path << " contains invalid config preset: '"
                   << local_android_info << "'.";
      return {};
    }
    return std::string{local_android_info};
  }

  ConfigReader& config_reader_;
  SystemImageDirFlag& system_image_dir_flag_;
  std::string config_;
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

fruit::Component<SystemImageDirFlag, ConfigFlag> ConfigFlagComponent() {
  return fruit::createComponent()
      .addMultibinding<FlagFeature, ConfigReader>()
      .bind<ConfigFlag, ConfigFlagImpl>()
      .addMultibinding<FlagFeature, ConfigFlag>()
      .bind<SystemImageDirFlag, SystemImageDirFlagImpl>()
      .addMultibinding<FlagFeature, SystemImageDirFlag>();
}

fruit::Component<ConfigFlag> ConfigFlagPlaceholder() {
  return fruit::createComponent()
      .addMultibinding<FlagFeature, ConfigFlag>()
      .bind<ConfigFlag, ConfigFlagPlaceholderImpl>();
}

}  // namespace cuttlefish
