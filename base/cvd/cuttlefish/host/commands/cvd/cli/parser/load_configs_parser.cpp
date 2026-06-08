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
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include "absl/strings/str_join.h"
#include <fmt/format.h>
#include <json/value.h>
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/flag_parser/gflags_compat.h"
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

Flag GflagsCompatFlagOverride(
    const std::string& name,
    std::map<std::string, std::string, std::less<void>>& overrides) {
  return Flag::StringFlag(name)
      .Getter([&overrides]() {
        std::vector<std::string> formatted;
        for (const auto& [k, v] : overrides) {
          formatted.push_back(fmt::format("({}=\"{}\")", k, v));
        }
        return absl::StrJoin(formatted, ",");
      })
      .Setter([&overrides](std::string_view arg) -> Result<void> {
        size_t separator_index = arg.find(kOverrideSeparator);
        CF_EXPECTF(separator_index != std::string::npos,
                   "Unable to find separator \"{}\" in input \"{}\"",
                   kOverrideSeparator, arg);
        auto property_path = arg.substr(0, separator_index);
        auto new_value = arg.substr(separator_index + 1);
        CF_EXPECTF(overrides.count(property_path) == 0,
                   "Property \"{}\" is already overridden", property_path);
        CF_EXPECTF(!property_path.empty(),
                   "Config path before the separator \"{}\" cannot be empty in "
                   "input \"{}\"",
                   kOverrideSeparator, arg);
        CF_EXPECTF(!new_value.empty(),
                   "New value after the separator \"{}\" cannot be empty in "
                   "input \"{}\"",
                   kOverrideSeparator, arg);
        CF_EXPECTF(property_path.front() != '.' && property_path.back() != '.',
                   "Config path \"{}\" must not start or end with dot",
                   property_path);
        CF_EXPECTF(property_path.find("..") == std::string::npos,
                   "Config path \"{}\" cannot contain two consecutive dots",
                   property_path);
        overrides[std::string(property_path)] = new_value;
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
    if (!absl::SimpleAtoi(leafValue, &leaf_val)) {
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
      if (!absl::SimpleAtoi(index, &index_val)) {
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

Result<Json::Value> ParseJsonFile(const std::string& file_path) {
  CF_EXPECTF(FileExists(file_path),
             "Provided file \"{}\" to cvd command does not exist", file_path);

  std::string file_content = CF_EXPECTF(
      ReadFileContents(file_path), "Failed to read file \"{}\"", file_path);
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
    const std::map<std::string, std::string, std::less<void>>& override_flags) {
  Json::Value result = CF_EXPECT(ParseJsonFile(config_path));

  for (const auto& [key, val] : override_flags) {
    MergeTwoJsonObjs(result, OverrideToJson(key, val));
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

std::vector<Flag> BuildCvdLoadFlags(LoadFlags& load_flags) {
  std::vector<Flag> flags;
  flags.emplace_back(
      GflagsCompatFlagOverride("override", load_flags.overrides)
          .Help(
              "Override or add new properties to the group specification. This "
              "flag may appear multiple times to affect different properties. "
              "The format is `--override=<path.to.property>:<value>`. "
              "For example: `--override=instance.0.vm.cpus=16`."));
  flags.emplace_back(
      Flag::StringFlag("credential_source")
          .Help("Source of credentials to access the Android Build Server API. "
                "Can be left empty in most cases, see the help for `login` and "
                "`fetch` for details.")
          .Setter([&load_flags](std::string_view arg) -> Result<void> {
            CF_EXPECTF(
                load_flags.overrides.count(kCredentialSourceOverride) == 0,
                "Specifying both --override={} and the "
                "--credential_source flag is not allowed.",
                kCredentialSourceOverride);
            load_flags.overrides.emplace(kCredentialSourceOverride, arg);
            return {};
          })
          .Getter([&load_flags]() -> std::string {
            auto it = load_flags.overrides.find(kCredentialSourceOverride);
            if (it != load_flags.overrides.end()) {
              return it->second;
            }
            return "";
          }));
  flags.emplace_back(
      Flag::StringFlag("project_id")
          .Help("Google Cloud Project ID for Android Build "
                "Server API access and quotas.")
          .Setter([&load_flags](std::string_view arg) -> Result<void> {
            CF_EXPECTF(load_flags.overrides.count(kProjectIDOverride) == 0,
                       "Specifying both --override={} and the --project_id "
                       "flag is not allowed.",
                       kProjectIDOverride);
            load_flags.overrides.emplace(kProjectIDOverride, arg);
            return {};
          })
          .Getter([&load_flags]() -> std::string {
            auto it =
                load_flags.overrides.find(kProjectIDOverride);
            if (it != load_flags.overrides.end()) {
              return it->second;
            }
            return "";
          }));
  flags.emplace_back(
      GflagsCompatFlag("base_directory", load_flags.base_dir)
          .Help(fmt::format(
              "Parent directory for artifacts and runtime files. When not "
              "provided a new directory under {}/{} will be created.",
              CvdDir(), getuid())));
  return flags;
}

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

Result<EnvironmentSpecification> GetEnvironmentSpecification(
    const std::string& config_path,
    const std::map<std::string, std::string, std::less<void>>& overrides) {
  Json::Value json_configs =
      CF_EXPECT(GetOverriddenConfig(config_path, overrides));

  EnvironmentSpecification env_spec =
      CF_EXPECT(ValidateCfConfigs(json_configs));
  return FillEmptyInstanceNames(std::move(env_spec));
}

}  // namespace cuttlefish
