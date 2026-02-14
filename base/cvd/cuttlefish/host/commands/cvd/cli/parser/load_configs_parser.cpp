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

#include "cuttlefish/host/commands/cvd/cli/parser/load_configs_parser.h"

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fmt/format.h>
#include <json/value.h>
#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/commands/cvd/cli/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/cli/parser/cf_flags_validator.h"
#include "cuttlefish/host/commands/cvd/cli/parser/fetch_config_parser.h"
#include "cuttlefish/host/commands/cvd/cli/parser/launch_cvd_parser.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/host/commands/cvd/cli/parser/selector_parser.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

using cvd::config::EnvironmentSpecification;

namespace {

constexpr std::string_view kOverrideSeparator = ":";
constexpr std::string_view kCredentialSourceOverride =
    "fetch.credential_source";
constexpr std::string_view kProjectIDOverride = "fetch.project_id";

bool IsLocalBuild(std::string path) {
  return absl::StartsWith(path, "/");
}

Flag GflagsCompatFlagOverride(const std::string& name,
                              std::vector<Override>& values) {
  return GflagsCompatFlag(name)
      .Getter([&values]() { return android::base::Join(values, ','); })
      .Setter([&values](const FlagMatch& match) -> Result<void> {
        size_t separator_index = match.value.find(kOverrideSeparator);
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
// double , float , etc) if needed
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
    uint32_t leaf_val{};
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
      uint32_t index_val{};
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
  flags.emplace_back(
      GflagsCompatFlag("credential_source", load_flags.credential_source));
  flags.emplace_back(GflagsCompatFlag("project_id", load_flags.project_id));
  flags.emplace_back(
      GflagsCompatFlag("base_directory", load_flags.base_dir)
          .Help(
              "Parent directory for artifacts and runtime files. Defaults to " +
              CvdDir() + "<uid>/<timestamp>."));
  flags.emplace_back(GflagsCompatFlagOverride("override", load_flags.overrides)
                         .Help("Use --override=<config_identifier>:<new_value> "
                               "to override config values"));
  return flags;
}

void MakeAbsolute(std::string& path, const std::string& working_dir) {
  if (!path.empty() && path[0] == '/') {
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

Result<std::vector<std::string>> GetSystemImagePaths(
    const EnvironmentSpecification& config) {
  std::vector<std::string> system_image_paths;
  for (const auto& instance : config.instances()) {
    CF_EXPECT(instance.disk().has_default_build());
    system_image_paths.emplace_back(instance.disk().default_build());
  }
  return system_image_paths;
}

std::optional<std::string> GetSystemHostPath(
    const EnvironmentSpecification& config) {
  if (config.common().has_host_package()) {
    return config.common().host_package();
  } else {
    return std::nullopt;
  }
}

Result<Json::Value> GetOverriddenConfig(
    const std::string& config_path,
    const std::vector<Override>& override_flags) {
  Json::Value result = CF_EXPECT(ParseJsonFile(config_path));

  if (!override_flags.empty()) {
    for (const auto& flag : override_flags) {
      MergeTwoJsonObjs(result,
                       OverrideToJson(flag.config_path, flag.new_value));
    }
  }

  return result;
}

EnvironmentSpecification FillEmptyInstanceNames(
    EnvironmentSpecification env_spec) {
  std::set<std::string_view> used;
  for (const auto& instance : env_spec.instances()) {
    if (instance.name().empty()) {
      continue;
    }
    used.insert(instance.name());
  }
  int index = 1;
  for (auto& instance : *env_spec.mutable_instances()) {
    if (!instance.name().empty()) {
      continue;
    }
    while (used.find(std::to_string(index)) != used.end()) {
      ++index;
    }
    std::string name = std::to_string(index++);
    instance.set_name(name);
    used.insert(instance.name());
  }
  return env_spec;
}

}  // namespace

Result<InstanceManager::GroupDirectories> GetGroupCreationDirectories(
    const std::string& parent_directory,
    const EnvironmentSpecification& env_spec) {
  std::vector<std::string> system_image_path_configs =
      CF_EXPECT(GetSystemImagePaths(env_spec));
  std::optional<std::string> system_host_path = GetSystemHostPath(env_spec);

  CF_EXPECT(!system_image_path_configs.empty(),
            "No instances in config to load");

  std::vector<std::optional<std::string>> targets_opt;
  int num_remote = 0;
  for (const auto& instance_build_path : system_image_path_configs) {
    if (IsLocalBuild(instance_build_path)) {
      targets_opt.emplace_back(instance_build_path);
    } else {
      targets_opt.emplace_back();
      num_remote++;
    }
  }

  CF_EXPECT(system_host_path || num_remote > 0,
            "Host tools path must be provided when using only local artifacts");

  std::optional<std::string> parent_dir_opt;
  if (!parent_directory.empty()) {
    parent_dir_opt.emplace(parent_directory);
  }
  std::optional<std::string> host_tools_opt;
  if (system_host_path && IsLocalBuild(system_host_path.value())) {
    // If config specifies a host tools path, we use this.
    host_tools_opt = std::move(system_host_path);
  }

  return InstanceManager::GroupDirectories{
      .base_directory = std::move(parent_dir_opt),
      .home = std::nullopt,
      .host_artifacts_path = std::move(host_tools_opt),
      .product_out_paths = std::move(targets_opt),
  };
}

Result<CvdFlags> ParseCvdConfigs(const EnvironmentSpecification& env_spec,
                                 const LocalInstanceGroup& group) {
  // TODO(jemoreira): Move this logic to LocalInstanceGroup or InstanceManager
  // to avoid duplication
  std::string target_directory =
      android::base::Dirname(group.HomeDir()) + "/artifacts";
  std::vector<std::string> target_subdirectories;
  for (int i = 0; i < group.Instances().size(); ++i) {
    target_subdirectories.emplace_back(std::to_string(i));
  }

  CvdFlags flags{
      .launch_cvd_flags = CF_EXPECT(ParseLaunchCvdConfigs(env_spec)),
      .selector_flags = ParseSelectorConfigs(env_spec),
      .fetch_cvd_flags = CF_EXPECT(ParseFetchCvdConfigs(
          env_spec, target_directory, target_subdirectories)),
      .target_directory = target_directory,
  };
  return flags;
}

std::ostream& operator<<(std::ostream& out, const Override& override) {
  fmt::print(out, "(config_path=\"{}\", new_value=\"{}\")",
             override.config_path, override.new_value);
  return out;
}

Result<LoadFlags> GetFlags(std::vector<std::string>& args,
                           const std::string& working_directory) {
  LoadFlags load_flags;
  auto flags = GetFlagsVector(load_flags);
  CF_EXPECT(ConsumeFlags(flags, args));
  CF_EXPECT(
      !args.empty(),
      "No arguments provided to cvd command, please provide path to json file");

  if (!load_flags.base_dir.empty()) {
    MakeAbsolute(load_flags.base_dir, working_directory);
  }

  load_flags.config_path = args.front();
  MakeAbsolute(load_flags.config_path, working_directory);

  if (!load_flags.credential_source.empty()) {
    for (const auto& flag : load_flags.overrides) {
      CF_EXPECT(!absl::StartsWith(flag.config_path,
                                           kCredentialSourceOverride),
                "Specifying both --override=fetch.credential_source and the "
                "--credential_source flag is not allowed.");
    }
    load_flags.overrides.emplace_back(
        Override{.config_path = std::string(kCredentialSourceOverride),
                 .new_value = load_flags.credential_source});
  }
  if (!load_flags.project_id.empty()) {
    for (const auto& flag : load_flags.overrides) {
      CF_EXPECT(
          !absl::StartsWith(flag.config_path, kProjectIDOverride),
          "Specifying both --override=fetch.project_id and the "
          "--project_id flag is not allowed.");
    }
    load_flags.overrides.emplace_back(
        Override{.config_path = std::string(kProjectIDOverride),
                 .new_value = load_flags.project_id});
  }
  return load_flags;
}

Result<EnvironmentSpecification> GetEnvironmentSpecification(
    const LoadFlags& flags) {
  Json::Value json_configs =
      CF_EXPECT(GetOverriddenConfig(flags.config_path, flags.overrides));

  EnvironmentSpecification env_spec =
      CF_EXPECT(ValidateCfConfigs(json_configs));
  return FillEmptyInstanceNames(std::move(env_spec));
}

}  // namespace cuttlefish
