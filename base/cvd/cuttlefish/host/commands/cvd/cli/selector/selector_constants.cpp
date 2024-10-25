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

#include "host/commands/cvd/cli/selector/selector_constants.h"

#include <sys/stat.h>
#include <unistd.h>

#include <sstream>

#include <android-base/errors.h>

namespace cuttlefish {
namespace selector {

enum class OwnershipType { kUser, kGroup, kOthers };

CvdFlag<std::string> SelectorFlags::GroupNameFlag(const std::string& name) {
  CvdFlag<std::string> group_name{name};
  std::stringstream group_name_help;
  group_name_help << "--" << name << "=<"
                  << "name of the instance group>";
  group_name.SetHelpMessage(group_name_help.str());
  return group_name;
}

CvdFlag<std::string> SelectorFlags::InstanceNameFlag(const std::string& name) {
  CvdFlag<std::string> instance_name{name};
  std::stringstream instance_name_help;
  instance_name_help << "--" << name << "=<"
                     << "comma-separated names of the instances>";
  instance_name.SetHelpMessage(instance_name_help.str());
  return instance_name;
}

const SelectorFlags& SelectorFlags::Get() {
  static Result<SelectorFlags> singleton = New();
  CHECK(singleton.ok()) << singleton.error().FormatForEnv();
  return *singleton;
}

Result<SelectorFlags> SelectorFlags::New() {
  SelectorFlags selector_flags;
  CF_EXPECT(selector_flags.flags_.EnrollFlag(GroupNameFlag(kGroupName)));
  CF_EXPECT(selector_flags.flags_.EnrollFlag(InstanceNameFlag(kInstanceName)));
  CF_EXPECT(selector_flags.flags_.EnrollFlag(VerbosityFlag(kVerbosity)));
  return selector_flags;
}

CvdFlag<std::string> SelectorFlags::VerbosityFlag(const std::string& name) {
  CvdFlag<std::string> verbosity_level(name);
  std::stringstream help;
  help << "--" << name << "=Severity for LOG(Severity) in the server.";
  verbosity_level.SetHelpMessage(help.str());
  return verbosity_level;
}

}  // namespace selector
}  // namespace cuttlefish
