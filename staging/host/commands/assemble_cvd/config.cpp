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

#include "host/commands/assemble_cvd/config.h"

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <gflags/gflags.h>
#include <json/json.h>
#include <fstream>
#include <set>
#include <string>

#include "common/libs/utils/files.h"
#include "host/libs/config/cuttlefish_config.h"

using google::FlagSettingMode::SET_FLAGS_DEFAULT;

DECLARE_string(system_image_dir);
DEFINE_string(config, "phone",
              "Config preset name. Will automatically set flag fields "
              "using the values from this file of presets. See "
              "device/google/cuttlefish/shared/config/config_*.json "
              "for possible values.");

namespace cuttlefish {
namespace {

bool IsFlagSet(const std::string& flag) {
  return !gflags::GetCommandLineFlagInfoOrDie(flag.c_str()).is_default;
}

}  // namespace

void SetDefaultFlagsFromConfigPreset() {
  std::string config_preset = FLAGS_config;  // The name of the preset config.
  std::string config_file_path;  // The path to the preset config JSON.
  std::set<std::string> allowed_config_presets;
  for (const std::string& file :
       DirectoryContents(DefaultHostArtifactsPath("etc/cvd_config"))) {
    std::string_view local_file(file);
    if (android::base::ConsumePrefix(&local_file, "cvd_config_") &&
        android::base::ConsumeSuffix(&local_file, ".json")) {
      allowed_config_presets.emplace(local_file);
    }
  }

  // If the user specifies a --config name, then use that config
  // preset option.
  std::string android_info_path = FLAGS_system_image_dir + "/android-info.txt";
  if (IsFlagSet("config")) {
    if (!allowed_config_presets.count(config_preset)) {
      LOG(FATAL) << "Invalid --config option '" << config_preset
                 << "'. Valid options: "
                 << android::base::Join(allowed_config_presets, ",");
    }
  } else if (FileExists(android_info_path)) {
    // Otherwise try to load the correct preset using android-info.txt.
    std::ifstream ifs(android_info_path);
    if (ifs.is_open()) {
      std::string android_info;
      ifs >> android_info;
      std::string_view local_android_info(android_info);
      if (android::base::ConsumePrefix(&local_android_info, "config=")) {
        config_preset = local_android_info;
      }
      if (!allowed_config_presets.count(config_preset)) {
        LOG(WARNING) << android_info_path
                     << " contains invalid config preset: '"
                     << local_android_info << "'. Defaulting to 'phone'.";
        config_preset = "phone";
      }
    }
  }
  LOG(INFO) << "Launching CVD using --config='" << config_preset << "'.";

  config_file_path = DefaultHostArtifactsPath("etc/cvd_config/cvd_config_" +
                                              config_preset + ".json");
  Json::Value config;
  Json::CharReaderBuilder builder;
  std::ifstream ifs(config_file_path);
  std::string errorMessage;
  if (!Json::parseFromStream(builder, ifs, &config, &errorMessage)) {
    LOG(FATAL) << "Could not read config file " << config_file_path << ": "
               << errorMessage;
  }
  for (const std::string& flag : config.getMemberNames()) {
    std::string value;
    if (flag == "custom_actions") {
      Json::StreamWriterBuilder factory;
      value = Json::writeString(factory, config[flag]);
    } else {
      value = config[flag].asString();
    }
    if (gflags::SetCommandLineOptionWithMode(flag.c_str(), value.c_str(),
                                             SET_FLAGS_DEFAULT)
            .empty()) {
      LOG(FATAL) << "Error setting flag '" << flag << "'.";
    }
  }
}

}  // namespace cuttlefish
