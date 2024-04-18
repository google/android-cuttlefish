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

#include "host/commands/cvd/selector/instance_group_record.h"

#include <set>

#include <android-base/parseint.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_database_utils.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/selector/instance_record.h"

namespace cuttlefish {
namespace selector {

namespace {

static constexpr const char kJsonGroupName[] = "Group Name";
static constexpr const char kJsonHomeDir[] = "Runtime/Home Dir";
static constexpr const char kJsonHostArtifactPath[] = "Host Tools Dir";
static constexpr const char kJsonProductOutPath[] = "Product Out Dir";
static constexpr const char kJsonStartTime[] = "Start Time";
static constexpr const char kJsonInstances[] = "Instances";
static constexpr const char kJsonParent[] = "Parent Group";

std::vector<LocalInstance> Filter(
    const std::vector<LocalInstance>& instances,
    std::function<bool(const LocalInstance&)> predicate) {
  std::vector<LocalInstance> ret;
  std::copy_if(instances.begin(), instances.end(), std::back_inserter(ret),
               predicate);
  return ret;
}

}  // namespace

Result<LocalInstanceGroup> LocalInstanceGroup::Create(
    const InstanceGroupInfo& group_info,
    const std::vector<InstanceInfo>& instance_infos) {
  CF_EXPECT(!instance_infos.empty(), "New group can't be empty");
  std::vector<LocalInstance> instances;
  std::set<unsigned> ids;
  std::set<std::string> names;

  for (const auto& instance_info : instance_infos) {
    instances.emplace_back(group_info, instance_info.id, instance_info.name);
    ids.insert(instance_info.id);
    names.insert(instance_info.name);
  }
  CF_EXPECT(ids.size() == instance_infos.size(),
            "Instances must have unique ids");
  CF_EXPECT(names.size() == instance_infos.size(),
            "Instances must have unique names");
  return LocalInstanceGroup(group_info.group_name, group_info.home_dir,
                            group_info.host_artifacts_path,
                            group_info.host_artifacts_path,
                            group_info.start_time, instances);
}

LocalInstanceGroup::LocalInstanceGroup(
    const std::string& group_name, const std::string& home_dir,
    const std::string& host_artifacts_path, const std::string& product_out_path,
    const TimeStamp& start_time, const std::vector<LocalInstance>& instances)
    : internal_group_name_(GenInternalGroupName()),
      group_info_{.group_name = group_name,
                  .home_dir = home_dir,
                  .host_artifacts_path = host_artifacts_path,
                  .product_out_path = product_out_path,
                  .start_time = start_time},
      instances_(instances) {};

std::vector<LocalInstance> LocalInstanceGroup::FindById(
    const unsigned id) const {
  return Filter(instances_, [id](const LocalInstance& instance) {
    return id == instance.InstanceId();
  });
}

std::vector<LocalInstance> LocalInstanceGroup::FindByInstanceName(
    const std::string& instance_name) const {
  return Filter(instances_, [instance_name](const LocalInstance& instance) {
    return instance.PerInstanceName() == instance_name;
  });
}

Json::Value LocalInstanceGroup::Serialize() const {
  Json::Value group_json;
  group_json[kJsonGroupName] = GroupName();
  group_json[kJsonHomeDir] = HomeDir();
  group_json[kJsonHostArtifactPath] = HostArtifactsPath();
  group_json[kJsonProductOutPath] = ProductOutPath();
  group_json[kJsonStartTime] = SerializeTimePoint(StartTime());

  int i = 0;
  Json::Value instances_array_json;
  for (const auto& instance : instances_) {
    Json::Value instance_json = Serialize(instance);
    instance_json[kJsonParent] = GroupName();
    instances_array_json[i] = instance_json;
    i++;
  }
  group_json[kJsonInstances] = instances_array_json;
  return group_json;
}

Json::Value LocalInstanceGroup::Serialize(const LocalInstance& instance) const {
  Json::Value instance_json;
  instance_json[LocalInstance::kJsonInstanceName] = instance.PerInstanceName();
  instance_json[LocalInstance::kJsonInstanceId] =
      std::to_string(instance.InstanceId());
  return instance_json;
}

Result<LocalInstanceGroup> LocalInstanceGroup::Deserialize(
    const Json::Value& group_json) {
  CF_EXPECT(group_json.isMember(kJsonGroupName));
  const std::string group_name = group_json[kJsonGroupName].asString();
  CF_EXPECT(group_json.isMember(kJsonHomeDir));
  const std::string home_dir = group_json[kJsonHomeDir].asString();
  CF_EXPECT(group_json.isMember(kJsonHostArtifactPath));
  const std::string host_artifacts_path =
      group_json[kJsonHostArtifactPath].asString();
  CF_EXPECT(group_json.isMember(kJsonProductOutPath));
  const std::string product_out_path =
      group_json[kJsonProductOutPath].asString();
  TimeStamp start_time = CvdServerClock::now();

  // test if the field is available as the field has been added
  // recently as of b/315855286
  if (group_json.isMember(kJsonStartTime)) {
    auto restored_start_time_result =
        DeserializeTimePoint(group_json[kJsonStartTime]);
    if (restored_start_time_result.ok()) {
      start_time = std::move(*restored_start_time_result);
    } else {
      LOG(ERROR) << "Start time restoration from json failed, so we use "
                 << " the current system time. Reasons: "
                 << restored_start_time_result.error().FormatForEnv();
    }
  }

  InstanceGroupInfo group_info({.group_name = group_name,
                                .home_dir = home_dir,
                                .host_artifacts_path = host_artifacts_path,
                                .product_out_path = product_out_path,
                                .start_time = std::move(start_time)});
  std::vector<InstanceInfo> instance_infos;
  CF_EXPECT(group_json.isMember(kJsonInstances));
  const Json::Value& instances_json_array = group_json[kJsonInstances];
  CF_EXPECT(instances_json_array.isArray());
  for (int i = 0; i < instances_json_array.size(); i++) {
    const Json::Value& instance_json = instances_json_array[i];
    CF_EXPECT(instance_json.isMember(LocalInstance::kJsonInstanceName));
    const std::string instance_name =
        instance_json[LocalInstance::kJsonInstanceName].asString();
    CF_EXPECT(instance_json.isMember(LocalInstance::kJsonInstanceId));
    const std::string instance_id =
        instance_json[LocalInstance::kJsonInstanceId].asString();

    int id;
    CF_EXPECTF(android::base::ParseInt(instance_id, std::addressof(id)),
               "Invalid instance ID in instance json: {}", instance_id);
    instance_infos.push_back({.id = (unsigned)id, .name = instance_name});
  }

  return Create(group_info, instance_infos);
}

}  // namespace selector
}  // namespace cuttlefish
