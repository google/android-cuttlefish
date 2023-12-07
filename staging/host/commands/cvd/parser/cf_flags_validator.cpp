/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/parser/cf_flags_validator.h"

#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flags_validator.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {
namespace {

using Json::ValueType::arrayValue;
using Json::ValueType::booleanValue;
using Json::ValueType::intValue;
using Json::ValueType::objectValue;
using Json::ValueType::stringValue;
using Json::ValueType::uintValue;

const auto& kRoot = *new ConfigNode{.type = objectValue, .children = {
  {"netsim_bt", ConfigNode{.type = booleanValue}},
  {"instances", ConfigNode{.type = arrayValue, .children = {
    {kArrayValidationSentinel, ConfigNode{.type = objectValue, .children = {
        {"@import", ConfigNode{.type = stringValue}},
        {"name", ConfigNode{.type = stringValue}},
        {"vm", ConfigNode{.type = objectValue, .children = {
          {"cpus", ConfigNode{.type = uintValue}},
          {"memory_mb", ConfigNode{.type = uintValue}},
          {"use_sdcard", ConfigNode{.type = booleanValue}},
          {"setupwizard_mode", ConfigNode{.type = stringValue}},
          {"uuid", ConfigNode{.type = stringValue}},
          {"crosvm", ConfigNode{.type = objectValue, .children = {
            {"enable_sandbox", ConfigNode{.type = booleanValue}},
          }}},
          {"custom_actions", ConfigNode{.type = arrayValue, .children = {
            {kArrayValidationSentinel, ConfigNode{.type = objectValue, .children = {
              {"shell_command", ConfigNode{.type = stringValue}},
              {"button", ConfigNode{.type = objectValue, .children = {
                {"command", ConfigNode{.type = stringValue}},
                {"title", ConfigNode{.type = stringValue}},
                {"icon_name", ConfigNode{.type = stringValue}},
              }}},
              {"server", ConfigNode{.type = stringValue}},
              {"buttons", ConfigNode{.type = arrayValue, .children = {
                {kArrayValidationSentinel, ConfigNode{.type = objectValue, .children = {
                  {"command", ConfigNode{.type = stringValue}},
                  {"title", ConfigNode{.type = stringValue}},
                  {"icon_name", ConfigNode{.type = stringValue}},
                }}},
              }}},
              {"device_states", ConfigNode{.type = arrayValue, .children = {
                {kArrayValidationSentinel, ConfigNode{.type = objectValue, .children = {
                  {"lid_switch_open", ConfigNode{.type = booleanValue}},
                  {"hinge_angle_value", ConfigNode{.type = intValue}},
                }}},
              }}},
            }}},
          }}},
        }}},
        {"boot", ConfigNode{.type = objectValue, .children = {
          {"kernel", ConfigNode{.type = objectValue, .children = {
            {"build", ConfigNode{.type = stringValue}},
          }}},
          {"enable_bootanimation", ConfigNode{.type = booleanValue}},
          {"build", ConfigNode{.type = stringValue}},
          {"bootloader", ConfigNode{.type = objectValue, .children = {
            {"build", ConfigNode{.type = stringValue}},
          }}},
        }}},
        {"security", ConfigNode{.type = objectValue, .children = {
          {"serial_number", ConfigNode{.type = stringValue}},
          {"use_random_serial", ConfigNode{.type = stringValue}},
          {"guest_enforce_security", ConfigNode{.type = booleanValue}},
        }}},
        {"disk", ConfigNode{.type = objectValue, .children = {
          {"default_build", ConfigNode{.type = stringValue}},
          {"super", ConfigNode{.type = objectValue, .children = {
            {"system", ConfigNode{.type = stringValue}},
          }}},
          {"download_img_zip", ConfigNode{.type = booleanValue}},
          {"download_target_zip_files", ConfigNode{.type = booleanValue}},
          {"blank_data_image_mb", ConfigNode{.type = uintValue}},
          {"otatools", ConfigNode{.type = stringValue}},
        }}},
        {"graphics", ConfigNode{.type = objectValue, .children = {
          {"displays", ConfigNode{.type = arrayValue, .children = {
            {kArrayValidationSentinel, ConfigNode{.type = objectValue, .children {
              {"width", ConfigNode{.type = uintValue}},
              {"height", ConfigNode{.type = uintValue}},
              {"dpi", ConfigNode{.type = uintValue}},
              {"refresh_rate_hertz", ConfigNode{.type = uintValue}},
            }}},
          }}},
          {"record_screen", ConfigNode{.type = booleanValue}},
        }}},
        {"streaming", ConfigNode{.type = objectValue, .children = {
          {"device_id", ConfigNode{.type = stringValue}},
        }}},
      }}},
    }}},
  {"fetch", ConfigNode{.type = objectValue, .children = {
      {"api_key", ConfigNode{.type = stringValue}},
      {"credential_source", ConfigNode{.type = stringValue}},
      {"wait_retry_period", ConfigNode{.type = uintValue}},
      {"external_dns_resolver", ConfigNode{.type = booleanValue}},
      {"keep_downloaded_archives", ConfigNode{.type = booleanValue}},
      {"api_base_url", ConfigNode{.type = stringValue}},
    }}},
  {"metrics", ConfigNode{.type = objectValue, .children = {
      {"enable", ConfigNode{.type = booleanValue}},
    }}},
  {"common", ConfigNode{.type = objectValue, .children = {
    {"group_name", ConfigNode{.type = stringValue}},
    {"host_package", ConfigNode{.type = stringValue}},
    {"bootconfig_args", ConfigNode{.type = stringValue}},
  }}},
},
};

}  // namespace

Result<void> ValidateCfConfigs(const Json::Value& root) {
  static const auto& kSupportedImportValues =
      *new std::unordered_set<std::string>{"phone", "tablet", "tv", "wearable",
                                           "auto",  "slim",   "go", "foldable"};

  CF_EXPECT(Validate(root, kRoot), "Validation failure in [root object] ->");
  for (const auto& instance : root["instances"]) {
    // TODO(chadreynolds): update `ExtractLaunchTemplates` to return a Result
    // and check import values there, then remove this check
    if (instance.isMember("@import")) {
      const std::string import_value = instance["@import"].asString();
      CF_EXPECTF(kSupportedImportValues.find(import_value) !=
                     kSupportedImportValues.end(),
                 "import value of \"{}\" is not supported", import_value);
    }
    CF_EXPECT(ValidateConfig<std::string>(instance, ValidateSetupWizardMode,
                                          {"vm", "setupwizard_mode"}),
              "Invalid value for setupwizard_mode flag");
  }
  return {};
}

}  // namespace cuttlefish
