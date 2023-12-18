/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/parser/load_configs_parser.h"

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fmt/format.h>
#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/fetch/fetch_cvd.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/cf_flags_validator.h"
#include "host/commands/cvd/parser/fetch_config_parser.h"
#include "host/commands/cvd/parser/launch_cvd_parser.h"
#include "host/commands/cvd/parser/selector_parser.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kOverrideSeparator = ":";
constexpr std::string_view kCredentialSourceOverride =
    "fetch.credential_source";

bool IsLocalBuild(std::string path) {
  return android::base::StartsWith(path, "/");
}

Flag GflagsCompatFlagOverride(const std::string& name,
                              std::vector<Override>& values) {
  return GflagsCompatFlag(name)
      .Getter([&values]() { return android::base::Join(values, ','); })
      .Setter([&values](const FlagMatch& match) -> Result<void> {
        std::size_t separator_index = match.value.find(kOverrideSeparator);
        CF_EXPECTF(separator_index != std::string::npos,
                   "Unable to find separator \"{}\" in input \"{}\"",
                   kOverrideSeparator, match.value);
        auto result =
            Override{.config_path = match.value.substr(0, separator_index),
                     .new_value = match.value.substr(separator_index + 1)};
        CF_EXPECTF(!result.config_path.empty(),
                   "Config path before the separator \"{}\" cannot be empty in "
                   "input \"{}\"",
                   kOverrideSeparator, match.value);
        CF_EXPECTF(!result.new_value.empty(),
                   "New value after the separator \"{}\" cannot be empty in "
                   "input \"{}\"",
                   kOverrideSeparator, match.value);
        CF_EXPECTF(result.config_path.front() != '.' &&
                       result.config_path.back() != '.',
                   "Config path \"{}\" must not start or end with dot",
                   result.config_path);
        CF_EXPECTF(result.config_path.find("..") == std::string::npos,
                   "Config path \"{}\" cannot contain two consecutive dots",
                   result.config_path);
        values.emplace_back(result);
        return {};
      });
}

// TODO(moelsherif): expand this enum in the future to support more types (
// double , float , etc) if neeeded
enum ArgValueType { UINTEGER, BOOLEAN, TEXT };

bool IsUnsignedInteger(const std::string& str) {
  return !str.empty() && std::all_of(str.begin(), str.end(),
                                     [](char c) { return std::isdigit(c); });
}

ArgValueType GetArgValueType(const std::string& str) {
  if (IsUnsignedInteger(str)) {
    return UINTEGER;
  }

  if (str == "true" || str == "false") {
    return BOOLEAN;
  }

  // Otherwise, treat the string as text
  return TEXT;
}

Json::Value OverrideToJson(const std::string& key,
                           const std::string& leafValue) {
  std::stack<std::string> levels;
  std::stringstream ks(key);
  std::string token;
  while (std::getline(ks, token, '.')) {
    levels.push(token);
  }

  // assign the leaf value based on the type of input value
  Json::Value leaf;
  if (GetArgValueType(leafValue) == UINTEGER) {
    std::uint32_t leaf_val{};
    if (!android::base::ParseUint(leafValue, &leaf_val)) {
      LOG(ERROR) << "Failed to parse unsigned integer " << leafValue;
      return Json::Value::null;
    };
    leaf = leaf_val;
  } else if (GetArgValueType(leafValue) == BOOLEAN) {
    leaf = (leafValue == "true");
  } else {
    leaf = leafValue;
  }

  while (!levels.empty()) {
    Json::Value curr;
    std::string index = levels.top();

    if (GetArgValueType(index) == UINTEGER) {
      std::uint32_t index_val{};
      if (!android::base::ParseUint(index, &index_val)) {
        LOG(ERROR) << "Failed to parse unsigned integer " << index;
        return Json::Value::null;
      }
      curr[index_val] = leaf;
    } else {
      curr[index] = leaf;
    }

    leaf = curr;
    levels.pop();
  }

  return leaf;
}

std::vector<Flag> GetFlagsVector(LoadFlags& load_flags) {
  std::vector<Flag> flags;
  flags.emplace_back(GflagsCompatFlag("help", load_flags.help));
  flags.emplace_back(
      GflagsCompatFlag("credential_source", load_flags.credential_source));
  flags.emplace_back(
      GflagsCompatFlag("base_directory", load_flags.base_dir)
          .Help("Parent directory for artifacts and runtime files. Defaults to "
                "/tmp/cvd/<uid>/<timestamp>."));
  flags.emplace_back(GflagsCompatFlagOverride("override", load_flags.overrides)
                         .Help("Use --override=<config_identifier>:<new_value> "
                               "to override config values"));
  return flags;
}

std::string DefaultBaseDir() {
  auto time = std::chrono::system_clock::now().time_since_epoch().count();
  std::stringstream ss;
  ss << "/tmp/cvd/" << getuid() << "/" << time;
  return ss.str();
}

void MakeAbsolute(std::string& path, const std::string& working_dir) {
  if (path.size() > 0 && path[0] == '/') {
    return;
  }
  path.insert(0, working_dir + "/");
}

Result<Json::Value> ParseJsonFile(const std::string& file_path) {
  CF_EXPECTF(FileExists(file_path),
             "Provided file \"{}\" to cvd command does not exist", file_path);

  std::string file_content;
  using android::base::ReadFileToString;
  CF_EXPECTF(ReadFileToString(file_path.c_str(), &file_content,
                              /* follow_symlinks */ true),
             "Failed to read file \"{}\"", file_path);
  auto root = CF_EXPECTF(ParseJson(file_content),
                         "Failed parsing file \"{}\" as JSON", file_path);
  return root;
}

Result<std::vector<std::string>> GetConfiguredSystemImagePaths(
    Json::Value& root) {
  return CF_EXPECTF(
      GetArrayValues<std::string>(root["instances"], {"disk", "default_build"}),
      "Instance is missing required Image path", "");
}

std::optional<std::string> GetConfiguredSystemHostPath(Json::Value& root) {
  auto result = GetValue<std::string>(root, {"common", "host_package"});
  if (result.ok()) {
    return std::optional<std::string>{*result};
  }
  return std::nullopt;
}

Result<Json::Value> GetOverriddenConfig(
    const std::string& config_path,
    const std::vector<Override>& override_flags) {
  Json::Value result = CF_EXPECT(ParseJsonFile(config_path));

  if (override_flags.size() > 0) {
    for (const auto& flag : override_flags) {
      MergeTwoJsonObjs(result,
                       OverrideToJson(flag.config_path, flag.new_value));
    }
  }

  return result;
}

Result<LoadDirectories> GenerateLoadDirectories(
    const std::string& parent_directory,
    std::vector<std::string>& system_image_path_configs,
    std::optional<std::string> system_host_path, const int num_instances) {
  CF_EXPECT_GT(num_instances, 0, "No instances in config to load");
  auto result = LoadDirectories{
      .target_directory = parent_directory + "/artifacts",
      .launch_home_directory = parent_directory + "/home",
  };

  std::vector<std::string> system_image_directories;
  int num_remote = 0;
  for (int i = 0; i < num_instances; i++) {
    const std::string instance_build_path = system_image_path_configs[i];
    CF_EXPECT_EQ(system_image_path_configs.size(), num_instances,
                 "Number of instances is inconsistent");

    auto target_subdirectory = std::to_string(i);
    result.target_subdirectories.emplace_back(target_subdirectory);
    if (IsLocalBuild(instance_build_path)) {
      system_image_directories.emplace_back(instance_build_path);
    } else {
      const std::string dir =
          result.target_directory + "/" + target_subdirectory;
      system_image_directories.emplace_back(dir);
      num_remote++;
    }
    LOG(INFO) << "Instance " << i << " directory is "
              << system_image_directories.back();
  }

  CF_EXPECT(system_host_path || num_remote > 0,
            "Host tools path must be provided when using only local artifacts");

  if (system_host_path && IsLocalBuild(system_host_path.value())) {
    // If config specifies a host tools path, we use this.
    result.host_package_directory = system_host_path.value();
  } else {
    result.host_package_directory =
        result.target_directory + "/" + kHostToolsSubdirectory;
  }

  result.system_image_directory_flag =
      "--system_image_dir=" +
      android::base::Join(system_image_directories, ',');
  return result;
}

Result<CvdFlags> ParseCvdConfigs(Json::Value& root,
                                 const LoadDirectories& load_directories) {
  CF_EXPECT(ValidateCfConfigs(root), "Loaded Json validation failed");
  return CvdFlags{.launch_cvd_flags = CF_EXPECT(ParseLaunchCvdConfigs(root)),
                  .selector_flags = CF_EXPECT(ParseSelectorConfigs(root)),
                  .fetch_cvd_flags = CF_EXPECT(ParseFetchCvdConfigs(
                      root, load_directories.target_directory,
                      load_directories.target_subdirectories)),
                  .load_directories = load_directories};
}

}  // namespace

std::ostream& operator<<(std::ostream& out, const Override& override) {
  fmt::print(out, "(config_path=\"{}\", new_value=\"{}\")",
             override.config_path, override.new_value);
  return out;
}

Result<LoadFlags> GetFlags(std::vector<std::string>& args,
                           const std::string& working_directory) {
  LoadFlags load_flags;
  auto flags = GetFlagsVector(load_flags);
  CF_EXPECT(ParseFlags(flags, args));
  CF_EXPECT(load_flags.help || args.size() > 0,
            "No arguments provided to cvd command, please provide at "
            "least one argument (help or path to json file)");

  if (load_flags.base_dir.empty()) {
    load_flags.base_dir = DefaultBaseDir();
  }
  MakeAbsolute(load_flags.base_dir, working_directory);

  load_flags.config_path = args.front();
  MakeAbsolute(load_flags.config_path, working_directory);

  if (!load_flags.credential_source.empty()) {
    for (const auto& flag : load_flags.overrides) {
      CF_EXPECT(!android::base::StartsWith(flag.config_path,
                                           kCredentialSourceOverride),
                "Specifying both --override=fetch.credential_source and the "
                "--credential_source flag is not allowed.");
    }
    load_flags.overrides.emplace_back(
        Override{.config_path = std::string(kCredentialSourceOverride),
                 .new_value = load_flags.credential_source});
  }
  return load_flags;
}

Result<CvdFlags> GetCvdFlags(const LoadFlags& flags) {
  Json::Value json_configs =
      CF_EXPECT(GetOverriddenConfig(flags.config_path, flags.overrides));

  std::vector<std::string> system_image_path_configs =
      CF_EXPECT(GetConfiguredSystemImagePaths(json_configs));
  std::optional<std::string> host_package_dir =
      GetConfiguredSystemHostPath(json_configs);

  const auto load_directories = CF_EXPECT(GenerateLoadDirectories(
      flags.base_dir, system_image_path_configs, host_package_dir,
      json_configs["instances"].size()));
  return CF_EXPECT(ParseCvdConfigs(json_configs, load_directories),
                   "Parsing json configs failed");
}

}  // namespace cuttlefish
