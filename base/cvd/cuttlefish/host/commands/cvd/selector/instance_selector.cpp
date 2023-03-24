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

#include "host/commands/cvd/selector/instance_selector.h"

#include <android-base/parseint.h>

#include "host/commands/cvd/selector/device_selector_utils.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace selector {

Result<LocalInstance::Copy> InstanceSelector::Select(
    const cvd_common::Args& selector_args, const uid_t uid,
    const InstanceDatabase& instance_database, const cvd_common::Envs& envs) {
  cvd_common::Args selector_args_copied{selector_args};
  SelectorCommonParser common_parser =
      CF_EXPECT(SelectorCommonParser::Parse(uid, selector_args_copied, envs));
  std::stringstream unused_args;
  unused_args << "{";
  for (const auto& arg : selector_args_copied) {
    unused_args << arg << ", ";
  }
  std::string unused_arg_list = unused_args.str();
  if (!selector_args_copied.empty()) {
    unused_arg_list.pop_back();
    unused_arg_list.pop_back();
  }
  unused_arg_list.append("}");
  if (!selector_args_copied.empty()) {
    LOG(ERROR) << "Warning: there are unused selector options. "
               << unused_arg_list;
  }
  InstanceSelector instance_selector(uid, std::move(common_parser), envs,
                                     instance_database);
  auto instance = CF_EXPECT(instance_selector.FindInstance());
  return instance;
}

bool InstanceSelector::IsHomeOverridden() const {
  auto home_overridden_result = common_parser_.HomeOverridden();
  if (!home_overridden_result.ok()) {
    return false;
  }
  return *home_overridden_result;
}

bool InstanceSelector::RequestsDefaultInstance() const {
  return !common_parser_.HasDeviceSelectOption() && !IsHomeOverridden() &&
         !HasCuttlefishInstance();
}

bool InstanceSelector::HasCuttlefishInstance() const {
  return Contains(envs_, kCuttlefishInstanceEnvVarName);
}

Result<LocalInstance::Copy> InstanceSelector::FindInstance() {
  if (RequestsDefaultInstance()) {
    auto default_instance = CF_EXPECT(FindDefaultInstance());
    return default_instance;
  }
  // search by instance and instances
  // search by HOME if overridden
  Queries queries;
  if (IsHomeOverridden()) {
    CF_EXPECT(common_parser_.Home());
    queries.emplace_back(kHomeField, common_parser_.Home().value());
  }
  if (common_parser_.GroupName()) {
    queries.emplace_back(kGroupNameField, common_parser_.GroupName().value());
  }
  if (common_parser_.PerInstanceNames()) {
    const auto per_instance_names = common_parser_.PerInstanceNames().value();
    CF_EXPECT_LE(per_instance_names.size(), 1,
                 "Instance Selector only picks up to 1 instance and thus "
                 "only take up to 1 instance_name");
    if (!per_instance_names.empty()) {
      queries.emplace_back(kInstanceNameField, per_instance_names.front());
    }
  }
  if (HasCuttlefishInstance()) {
    int id;
    const std::string instance_id_str = envs_.at(kCuttlefishInstanceEnvVarName);
    if (android::base::ParseInt(instance_id_str, std::addressof(id))) {
      queries.emplace_back(kInstanceIdField, std::to_string(id));
    } else {
      LOG(ERROR) << kCuttlefishInstanceEnvVarName << "=" << id
                 << " was given but it must have one valid instance ID.";
    }
  }
  // check CUTTLEFISH_INSTANCE envs
  CF_EXPECT(!queries.empty());
  auto instances = CF_EXPECT(instance_database_.FindInstances(queries));
  CF_EXPECT(instances.size() == 1, "instances.size() = " << instances.size());
  auto& instance = *(instances.cbegin());
  return instance.Get().GetCopy();
}

Result<LocalInstance::Copy> InstanceSelector::FindDefaultInstance() {
  auto group = CF_EXPECT(GetDefaultGroup(instance_database_, client_uid_));
  const auto instances = CF_EXPECT(group.FindAllInstances());
  CF_EXPECT_EQ(instances.size(), 1,
               "Default instance is the single instance in the default group.");
  return instances.cbegin()->Get().GetCopy();
}

}  // namespace selector
}  // namespace cuttlefish
