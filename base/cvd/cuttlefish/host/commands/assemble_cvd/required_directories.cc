//
// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/assemble_cvd/required_directories.h"

#include <unistd.h>
#include <string>
#include <vector>

#include "cuttlefish/host/libs/config/config_constants.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

std::vector<std::string> RequiredDirectories(
    const CuttlefishConfig::EnvironmentSpecific& environment) {
  return {
      environment.environment_dir(),                 //
      environment.environment_uds_dir(),             //
      environment.PerEnvironmentLogPath(""),         //
      environment.PerEnvironmentGrpcSocketPath(""),  //
  };
}

std::vector<std::string> RequiredDirectories(
    const CuttlefishConfig::InstanceSpecific& instance) {
  return {
      instance.instance_dir(),                                       //
      absl::StrCat(instance.instance_dir(), "/", kInternalDirName),  //
      absl::StrCat(instance.instance_dir(), "/", kSharedDirName),    //
      absl::StrCat(instance.instance_dir(), "/recording"),           //
      instance.PerInstanceLogPath(""),                               //
      instance.instance_uds_dir(),                                   //
      instance.instance_internal_uds_dir(),                          //
      instance.PerInstanceGrpcSocketPath(""),                        //
  };
}

}  // namespace

std::vector<std::string> RequiredDirectories(const CuttlefishConfig& config) {
  std::vector<std::string> required = {
      config.root_dir(),              //
      config.assembly_dir(),          //
      config.instances_dir(),         //
      config.instances_uds_dir(),     //
      config.environments_dir(),      //
      config.environments_uds_dir(),  //
  };

  for (std::string& dir : RequiredDirectories(config.ForDefaultEnvironment())) {
    required.emplace_back(std::move(dir));
  }

  for (const CuttlefishConfig::InstanceSpecific& instance :
       config.Instances()) {
    for (std::string& dir : RequiredDirectories(instance)) {
      required.emplace_back(std::move(dir));
    }
  }

  return required;
}

}  // namespace cuttlefish
