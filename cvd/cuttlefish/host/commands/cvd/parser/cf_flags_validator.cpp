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

#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flags_validator.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {
namespace {

static std::map<std::string, Json::ValueType> kConfigsKeyMap = {
    {"netsim_bt", Json::ValueType::booleanValue},
    {"instances", Json::ValueType::arrayValue},
    {"fetch", Json::ValueType::objectValue},
};

static std::map<std::string, Json::ValueType> kFetchKeyMap = {
    {"api_key", Json::ValueType::stringValue},
    {"credential", Json::ValueType::stringValue},
    {"wait_retry_period", Json::ValueType::uintValue},
    {"external_dns_resolver", Json::ValueType::booleanValue},
    {"keep_downloaded_archives", Json::ValueType::booleanValue},
};

static std::map<std::string, Json::ValueType> kInstanceKeyMap = {
    {"@import", Json::ValueType::stringValue},
    {"vm", Json::ValueType::objectValue},
    {"boot", Json::ValueType::objectValue},
    {"security", Json::ValueType::objectValue},
    {"disk", Json::ValueType::objectValue},
    {"graphics", Json::ValueType::objectValue},
    {"camera", Json::ValueType::objectValue},
    {"connectivity", Json::ValueType::objectValue},
    {"audio", Json::ValueType::objectValue},
    {"streaming", Json::ValueType::objectValue},
    {"adb", Json::ValueType::objectValue},
    {"vehicle", Json::ValueType::objectValue},
    {"location", Json::ValueType::objectValue}};

static std::unordered_set<std::string> kSupportedImportValues = {
    "phone", "tablet", "tv", "wearable", "auto", "slim", "go", "foldable"};

static std::map<std::string, Json::ValueType> kVmKeyMap = {
    {"cpus", Json::ValueType::uintValue},
    {"memory_mb", Json::ValueType::uintValue},
    {"use_sdcard", Json::ValueType::booleanValue},
    {"setupwizard_mode", Json::ValueType::stringValue},
    {"uuid", Json::ValueType::stringValue},
    {"crosvm", Json::ValueType::objectValue},
    {"qemu", Json::ValueType::objectValue},
    {"gem5", Json::ValueType::objectValue},
    {"custom_actions", Json::ValueType::arrayValue},
};
static std::map<std::string, Json::ValueType> kCrosvmKeyMap = {
    {"enable_sandbox", Json::ValueType::booleanValue},
};

static std::map<std::string, Json::ValueType> kBootKeyMap = {
    {"kernel", Json::ValueType::objectValue},
    {"enable_bootanimation", Json::ValueType::booleanValue},
    {"build", Json::ValueType::stringValue},
    {"bootloader", Json::ValueType::objectValue},
};

static std::map<std::string, Json::ValueType> kKernelKeyMap = {
    {"extra_kernel_cmdline", Json::ValueType::stringValue},
    {"build", Json::ValueType::stringValue},
};

static std::map<std::string, Json::ValueType> kBootloaderKeyMap = {
    {"build", Json::ValueType::stringValue},
};

static std::map<std::string, Json::ValueType> kGraphicsKeyMap = {
    {"displays", Json::ValueType::arrayValue},
    {"record_screen", Json::ValueType::booleanValue},
};
static std::map<std::string, Json::ValueType> kDisplayKeyMap = {
    {"width", Json::ValueType::uintValue},
    {"height", Json::ValueType::uintValue},
    {"dpi", Json::ValueType::uintValue},
    {"refresh_rate_hertz", Json::ValueType::uintValue},
};

static std::map<std::string, Json::ValueType> kSecurityKeyMap = {
    {"serial_number", Json::ValueType::stringValue},
    {"use_random_serial", Json::ValueType::stringValue},
    {"guest_enforce_security", Json::ValueType::booleanValue},
};

static std::map<std::string, Json::ValueType> kDiskKeyMap = {
    {"default_build", Json::ValueType::stringValue},
    {"super", Json::ValueType::objectValue},
    {"download_img_zip", Json::ValueType::booleanValue},
    {"download_target_zip_files", Json::ValueType::booleanValue},
    {"blank_data_image_mb", Json::ValueType::uintValue},
    {"otatools", Json::ValueType::stringValue},
    {"host_package", Json::ValueType::stringValue},
};

static std::map<std::string, Json::ValueType> kSuperKeyMap = {
    {"system", Json::ValueType::stringValue},
};

Result<void> ValidateSecurityConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kSecurityKeyMap),
            "ValidateSecurityConfigs ValidateTypo fail");
  return {};
}
Result<void> ValidateDiskConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kDiskKeyMap),
            "ValidateDiskConfigs ValidateTypo fail");
  if (root.isMember("super")) {
    CF_EXPECT(ValidateTypo(root["super"], kSuperKeyMap),
              "ValidateDiskSuperConfigs ValidateTypo fail");
  }
  return {};
}

Result<void> ValidateDisplaysConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kDisplayKeyMap),
            "ValidateDisplaysConfigs ValidateTypo fail");
  return {};
}

Result<void> ValidateGraphicsConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kGraphicsKeyMap),
            "ValidateGraphicsConfigs ValidateTypo fail");

  if (root.isMember("displays") && root["displays"].size() != 0) {
    for (const auto& display : root["displays"]) {
      CF_EXPECT(ValidateDisplaysConfigs(display));
    }
  }

  return {};
}

Result<void> ValidateVmConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kVmKeyMap),
            "ValidateVmConfigs ValidateTypo fail");
  if (root.isMember("crosvm")) {
    CF_EXPECT(ValidateTypo(root["crosvm"], kCrosvmKeyMap),
              "ValidateVmConfigs ValidateTypo crosvm fail");
  }
  return {};
}

Result<void> ValidateKernelConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kKernelKeyMap),
            "ValidateKernelConfigs ValidateTypo fail");
  return {};
}

Result<void> ValidateBootloaderConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kBootloaderKeyMap),
            "ValidateBootloaderConfigs ValidateTypo fail");
  return {};
}

Result<void> ValidateBootConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kBootKeyMap),
            "ValidateBootConfigs ValidateTypo fail");

  if (root.isMember("kernel")) {
    CF_EXPECT(ValidateKernelConfigs(root["kernel"]));
  }
  if (root.isMember("bootloader")) {
    CF_EXPECT(ValidateBootloaderConfigs(root["bootloader"]));
  }
  return {};
}

Result<void> ValidateInstancesConfigs(const Json::Value& instances) {
  for (const auto& instance : instances) {
    CF_EXPECT(ValidateTypo(instance, kInstanceKeyMap),
              "instance ValidateTypo fail");

    if (instance.isMember("vm")) {
      CF_EXPECT(ValidateVmConfigs(instance["vm"]));
    }

    if (instance.isMember("@import")) {
      CF_EXPECT(
          kSupportedImportValues.count(instance["@import"].asString()) > 0,
          "@Import flag values are not supported");
    }

    if (instance.isMember("boot")) {
      CF_EXPECT(ValidateBootConfigs(instance["boot"]));
    }
    if (instance.isMember("security")) {
      CF_EXPECT(ValidateSecurityConfigs(instance["security"]));
    }
    if (instance.isMember("disk")) {
      CF_EXPECT(ValidateDiskConfigs(instance["disk"]));
    }
    if (instance.isMember("graphics")) {
      CF_EXPECT(ValidateGraphicsConfigs(instance["graphics"]));
    }
    CF_EXPECT(ValidateConfig<std::string>(instance, ValidateSetupWizardMode,
                                          {"vm", "setupwizard_mode"}),
              "Invalid value for setupwizard_mode flag");
  }

  return {};
}

}  // namespace

Result<void> ValidateCfConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kConfigsKeyMap),
            "Typo in config main parameters");
  CF_EXPECT(ValidateTypo(root["fetch"], kFetchKeyMap),
            "Typo in config fetch parameters");
  CF_EXPECT(root.isMember("instances"), "instances object is missing");
  CF_EXPECT(ValidateInstancesConfigs(root["instances"]),
            "ValidateInstancesConfigs failed");

  return {};
}

}  // namespace cuttlefish
