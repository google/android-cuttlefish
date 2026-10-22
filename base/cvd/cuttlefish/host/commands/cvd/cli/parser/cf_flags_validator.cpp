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

#include "cuttlefish/host/commands/cvd/cli/parser/cf_flags_validator.h"

#include <sstream>
#include <string>

#include "google/protobuf/util/json_util.h"
#include "json/value.h"

#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

using cvd::config::Boot;
using cvd::config::Disk;
using cvd::config::DiskArtifacts;
using cvd::config::EnvironmentSpecification;
using cvd::config::Instance;
using google::protobuf::util::JsonStringToMessage;

bool HasLegacyArtifacts(const Instance& instance) {
  const Disk& disk = instance.disk();
  const Boot& boot = instance.boot();
  return disk.has_super_partition() || boot.has_kernel() || boot.has_build() ||
         boot.has_bootloader() || boot.has_android_efi_loader();
}

Result<void> ValidateDiskArtifacts(
    const EnvironmentSpecification& launch_config) {
  bool has_legacy_artifacts = false;
  bool has_disk_artifacts = false;
  for (const Instance& instance : launch_config.instances()) {
    if (HasLegacyArtifacts(instance)) {
      has_legacy_artifacts = true;
    }
    if (instance.disk().has_disk_artifacts()) {
      has_disk_artifacts = true;
      const DiskArtifacts& disk_artifacts = instance.disk().disk_artifacts();
      if (disk_artifacts.has_vendor_boot()) {
        CF_EXPECT(!disk_artifacts.vendor_boot().empty(),
                  "disk_artifacts vendor_boot build must not be empty");
      }
    }
  }
  CF_EXPECT(!(has_legacy_artifacts && has_disk_artifacts),
            "Cannot use disk_artifacts together with legacy disk/boot "
            "artifact options");
  return {};
}

}  // namespace

Result<EnvironmentSpecification> ValidateCfConfigs(const Json::Value& root) {
  std::stringstream json_as_stringstream;
  json_as_stringstream << root;
  auto json_str = json_as_stringstream.str();

  EnvironmentSpecification launch_config;
  auto status = JsonStringToMessage(json_str, &launch_config);
  CF_EXPECTF(status.ok(), "{}", status.ToString());
  CF_EXPECT(ValidateDiskArtifacts(launch_config));

  return launch_config;
}

}  // namespace cuttlefish
