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

#include <algorithm>
#include <set>

#include <android-base/parseint.h>

#include "common/libs/utils/result.h"
#include "host/commands/cvd/selector/instance_database_types.h"

namespace cuttlefish {
namespace selector {

namespace {

static constexpr const char kJsonGroupName[] = "Group Name";
static constexpr const char kJsonHomeDir[] = "Runtime/Home Dir";
static constexpr const char kJsonHostArtifactPath[] = "Host Tools Dir";
static constexpr const char kJsonProductOutPath[] = "Product Out Dir";
static constexpr const char kJsonStartTime[] = "Start Time";
static constexpr const char kJsonInstances[] = "Instances";
static constexpr const char kJsonInstanceId[] = "Instance Id";
static constexpr const char kJsonInstanceName[] = "Per-Instance Name";

std::vector<cvd::Instance> Filter(
    const std::vector<cvd::Instance>& instances,
    std::function<bool(const cvd::Instance&)> predicate) {
  std::vector<cvd::Instance> ret;
  std::copy_if(instances.begin(), instances.end(), std::back_inserter(ret),
               predicate);
  return ret;
}

}  // namespace

Result<LocalInstanceGroup> LocalInstanceGroup::Create(
    const cvd::InstanceGroup& group_proto) {
  CF_EXPECT(!group_proto.instances().empty(), "New group can't be empty");
  std::vector<cvd::Instance> instances;
  std::set<unsigned> ids;
  std::set<std::string> names;

  for (const auto& instance_proto : group_proto.instances()) {
    instances.push_back(instance_proto);
    auto id = instance_proto.id();
    if (id != 0) {
      // Only non-zero ids are checked, zero means no id has been assigned yet.
      CF_EXPECTF(ids.find(id) == ids.end(),
                 "Instances must have unique ids, found '{}' repeated", id);
      ids.insert(id);
    }
    names.insert(instance_proto.name());
  }
  CF_EXPECT(names.size() == (size_t)group_proto.instances_size(),
            "Instances must have unique names");
  return LocalInstanceGroup(group_proto, instances);
}

void LocalInstanceGroup::SetHomeDir(const std::string& home_dir) {
  CHECK(group_proto_.home_directory().empty())
      << "Home directory can't be changed once set";
  group_proto_.set_home_directory(home_dir);
}

void LocalInstanceGroup::SetHostArtifactsPath(
    const std::string& host_artifacts_path) {
  CHECK(group_proto_.host_artifacts_path().empty())
      << "Host artifacts path can't be changed once set";
  group_proto_.set_host_artifacts_path(host_artifacts_path);
}

void LocalInstanceGroup::SetProductOutPath(
    const std::string& product_out_path) {
  CHECK(group_proto_.product_out_path().empty())
      << "Product out path can't be changed once set";
  group_proto_.set_product_out_path(product_out_path);
}

bool LocalInstanceGroup::HasActiveInstances() const {
  // Active instances and only active instances have id > 0, no need to look at
  // instance state.
  return std::any_of(Instances().begin(), Instances().end(),
                     [](const auto& instance) { return instance.id() > 0; });
}

void LocalInstanceGroup::SetAllStates(cvd::InstanceState state) {
  for (auto& instance: Instances()) {
    instance.set_state(state);
  }
}

void LocalInstanceGroup::SetAllStatesAndResetIds(cvd::InstanceState state) {
  SetAllStates(state);
  for (auto& instance: Instances()) {
    instance.set_id(0);
  }
}

TimeStamp LocalInstanceGroup::StartTime() const {
  return CvdServerClock::from_time_t(group_proto_.start_time_sec());
}

void LocalInstanceGroup::SetStartTime(TimeStamp time) {
  group_proto_.set_start_time_sec(CvdServerClock::to_time_t(time));
}

LocalInstanceGroup::LocalInstanceGroup(
    const cvd::InstanceGroup& group_proto, const std::vector<cvd::Instance>& instances)
    : group_proto_(group_proto),
      instances_(instances) {};

std::vector<cvd::Instance> LocalInstanceGroup::FindById(
    const unsigned id) const {
  return Filter(instances_, [id](const cvd::Instance& instance) {
    return id == instance.id();
  });
}

std::vector<cvd::Instance> LocalInstanceGroup::FindByInstanceName(
    const std::string& instance_name) const {
  return Filter(instances_, [instance_name](const cvd::Instance& instance) {
    return instance.name() == instance_name;
  });
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

  cvd::InstanceGroup group_proto;
  group_proto.set_name(group_name);
  group_proto.set_home_directory(home_dir);
  group_proto.set_host_artifacts_path(host_artifacts_path);
  group_proto.set_product_out_path(product_out_path);
  group_proto.set_start_time_sec(CvdServerClock::to_time_t(start_time));

  CF_EXPECT(group_json.isMember(kJsonInstances));
  const Json::Value& instances_json_array = group_json[kJsonInstances];
  CF_EXPECT(instances_json_array.isArray());
  for (int i = 0; i < (int)instances_json_array.size(); i++) {
    const Json::Value& instance_json = instances_json_array[i];
    CF_EXPECT(instance_json.isMember(kJsonInstanceName));
    const std::string instance_name =
        instance_json[kJsonInstanceName].asString();
    CF_EXPECT(instance_json.isMember(kJsonInstanceId));
    const std::string instance_id = instance_json[kJsonInstanceId].asString();

    int id;
    CF_EXPECTF(android::base::ParseInt(instance_id, std::addressof(id)),
               "Invalid instance ID in instance json: {}", instance_id);
    auto instance = group_proto.add_instances();
    instance->set_id(id);
    instance->set_name(instance_name);
  }

  return Create(group_proto);
}

}  // namespace selector
}  // namespace cuttlefish
