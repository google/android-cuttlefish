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
#include <android-base/file.h>
#include <gflags/gflags.h>

#include <stdio.h>
#include <fstream>
#include <string>
#include <unordered_set>

#include "common/libs/utils/files.h"
#include "common/libs/utils/flags_validator.h"
#include "common/libs/utils/json.h"
#include "host/commands/cvd/parser/cf_configs_common.h"

namespace cuttlefish {

// json main parameters definitions
static std::map<std::string, Json::ValueType> kConfigsKeyMap = {
    {"credential", Json::ValueType::stringValue},
    {"netsim_bt", Json::ValueType::booleanValue},
    {"instances", Json::ValueType::arrayValue}};

// instance object parameters definitions
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

// supported import values for @import key
static std::unordered_set<std::string> kSupportedImportValues = {
    "phone", "tablet", "tv", "wearable", "auto", "slim", "go", "foldable"};

// supported import values for vm category and crosvm subcategory
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

// supported import values for boot category and kernel subcategory
static std::map<std::string, Json::ValueType> kBootKeyMap = {
    {"extra_bootconfig_args", Json::ValueType::stringValue},
    {"kernel", Json::ValueType::objectValue},
    {"enable_bootanimation", Json::ValueType::booleanValue},
};
static std::map<std::string, Json::ValueType> kernelkeyMap = {
    {"extra_kernel_cmdline", Json::ValueType::stringValue},
};

// supported import values for graphics category and displays subcategory
static std::map<std::string, Json::ValueType> kGraphicsKeyMap = {
    {"displays", Json::ValueType::arrayValue},
};
static std::map<std::string, Json::ValueType> kDisplayKeyMap = {
    {"width", Json::ValueType::uintValue},
    {"height", Json::ValueType::uintValue},
    {"dpi", Json::ValueType::uintValue},
    {"refresh_rate_hertz", Json::ValueType::uintValue},
};

// supported import values for security category
static std::map<std::string, Json::ValueType> kSecurityKeyMap = {
    {"serial_number", Json::ValueType::stringValue},
    {"guest_enforce_security", Json::ValueType::booleanValue},
};

// supported import values for disk category
static std::map<std::string, Json::ValueType> kDiskKeyMap = {
    {"default_build", Json::ValueType::stringValue},
    {"system_build", Json::ValueType::stringValue},
    {"kernel_build", Json::ValueType::stringValue},
};

// Validate the security json parameters
Result<void> ValidateSecurityConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kSecurityKeyMap),
            "ValidateSecurityConfigs ValidateTypo fail");
  return {};
}
Result<void> ValidateDiskConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kDiskKeyMap),
            "ValidateDiskConfigs ValidateTypo fail");
  return {};
}

// Validate the displays json parameters
Result<void> ValidateDisplaysConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kDisplayKeyMap),
            "ValidateDisplaysConfigs ValidateTypo fail");
  return {};
}

// Validate the graphics json parameters
Result<void> ValidateGraphicsConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kGraphicsKeyMap),
            "ValidateGraphicsConfigs ValidateTypo fail");

  if (root.isMember("displays") && root["displays"].size() != 0) {
    int num_displays = root["displays"].size();
    for (int i = 0; i < num_displays; i++) {
      CF_EXPECT(ValidateDisplaysConfigs(root["displays"][i]),
                "ValidateDisplaysConfigs fail");
    }
  }

  return {};
}

// Validate the vm json parameters
Result<void> ValidateVmConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kVmKeyMap),
            "ValidateVmConfigs ValidateTypo fail");
  if (root.isMember("crosvm")) {
    CF_EXPECT(ValidateTypo(root["crosvm"], kCrosvmKeyMap),
              "ValidateVmConfigs ValidateTypo crosvm fail");
  }
  return {};
}

// Validate the kernel json parameters
Result<void> ValidateKernelConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kernelkeyMap),
            "ValidateKernelConfigs ValidateTypo fail");
  return {};
}

// Validate the boot json parameters
Result<void> ValidateBootConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kBootKeyMap),
            "ValidateBootConfigs ValidateTypo fail");

  if (root.isMember("kernel")) {
    CF_EXPECT(ValidateKernelConfigs(root["kernel"]),
              "ValidateKernelConfigs fail");
  }

  return {};
}

// Validate the instances json parameters
Result<void> ValidateInstancesConfigs(const Json::Value& root) {
  int num_instances = root.size();
  for (unsigned int i = 0; i < num_instances; i++) {
    CF_EXPECT(ValidateTypo(root[i], kInstanceKeyMap), "vm ValidateTypo fail");

    if (root[i].isMember("vm")) {
      CF_EXPECT(ValidateVmConfigs(root[i]["vm"]), "ValidateVmConfigs fail");
    }

    // Validate @import flag values are supported or not
    if (root[i].isMember("@import")) {
      CF_EXPECT(kSupportedImportValues.count(root[i]["@import"].asString()) > 0,
                "@Import flag values are not supported");
    }

    if (root[i].isMember("boot")) {
      CF_EXPECT(ValidateBootConfigs(root[i]["boot"]),
                "ValidateBootConfigs fail");
    }
    if (root[i].isMember("security")) {
      CF_EXPECT(ValidateSecurityConfigs(root[i]["security"]),
                "ValidateSecurityConfigs fail");
    }
    if (root[i].isMember("disk")) {
      CF_EXPECT(ValidateDiskConfigs(root[i]["disk"]),
                "ValidateDiskConfigs fail");
    }
    if (root[i].isMember("graphics")) {
      CF_EXPECT(ValidateGraphicsConfigs(root[i]["graphics"]),
                "ValidateGraphicsConfigs fail");
    }
  }
  CF_EXPECT(ValidateStringConfig(root, "vm", "setupwizard_mode",
                                 ValidateStupWizardMode),
            "Invalid value for setupwizard_mode flag");

  return {};
}

// Validate cuttlefish config json parameters
Result<void> ValidateCfConfigs(const Json::Value& root) {
  CF_EXPECT(ValidateTypo(root, kConfigsKeyMap),
            "Typo in config main parameters");
  CF_EXPECT(root.isMember("instances"), "instances object is missing");
  CF_EXPECT(ValidateInstancesConfigs(root["instances"]),
            "ValidateInstancesConfigs failed");

  return {};
}

}  // namespace cuttlefish
