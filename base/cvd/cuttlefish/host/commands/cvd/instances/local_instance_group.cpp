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

#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"

#include <algorithm>
#include <functional>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/strings.h>
#include <json/json.h>

#include "cuttlefish/host/commands/cvd/instances/instance_database_types.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

std::vector<LocalInstance> Filter(
    const std::vector<LocalInstance>& instances,
    std::function<bool(const LocalInstance&)> predicate) {
  std::vector<LocalInstance> ret;
  std::copy_if(instances.begin(), instances.end(), std::back_inserter(ret),
               predicate);
  return ret;
}

std::string DefaultBaseDir() {
  auto time = std::chrono::system_clock::now().time_since_epoch().count();
  return fmt::format("{}/{}", PerUserDir(), time);
}

std::string HomeDirFromBase(const std::string& base_dir) {
  return base_dir + "/home";
}

std::string ArtifactsDirFromBase(const std::string& base_dir) {
  return base_dir + "/artifacts";
}

std::string HostArtifactsDirFromBase(const std::string& base_dir) {
  return ArtifactsDirFromBase(base_dir) + "/host_tools";
}

std::string ProductDirFromBase(const std::string& base_dir,
                               int instance_index) {
  return fmt::format("{}/{}", ArtifactsDirFromBase(base_dir),
                     std::to_string(instance_index));
}

}  // namespace

Result<LocalInstanceGroup> LocalInstanceGroup::Create(
    const cvd::InstanceGroup& group_proto) {
  CF_EXPECT(!group_proto.instances().empty(), "New group can't be empty");
  std::set<unsigned> ids;
  std::set<std::string> names;

  for (const auto& instances : group_proto.instances()) {
    auto id = instances.id();
    CF_EXPECT_GE(id, 1, "Instance ids must be positive");
    // Only non-zero ids are checked, zero means no id has been assigned yet.
    CF_EXPECTF(ids.find(id) == ids.end(),
               "Instances must have unique ids, found '{}' repeated", id);
    ids.insert(id);
    auto name = instances.name();
    CF_EXPECTF(names.find(name) == names.end(),
               "Instances must have unique names, found '{}' repeated", name);
    names.insert(name);
  }
  return LocalInstanceGroup(group_proto);
}

LocalInstanceGroup::Builder::Builder(std::string group_name)
    : base_dir_(DefaultBaseDir()) {
  group_proto_.set_name(std::move(group_name));
  group_proto_.set_home_directory(HomeDirFromBase(base_dir_));
  group_proto_.set_host_artifacts_path(HostArtifactsDirFromBase(base_dir_));
}

LocalInstanceGroup::Builder& LocalInstanceGroup::Builder::AddInstance(
    unsigned id) & {
  return AddInstance(id, std::to_string(id));
}

LocalInstanceGroup::Builder& LocalInstanceGroup::Builder::AddInstance(
    unsigned id, std::string name) & {
  auto& new_instance = *group_proto_.add_instances();
  new_instance.set_id(id);
  new_instance.set_name(std::move(name));
  new_instance.set_state(cvd::INSTANCE_STATE_PREPARING);
  return *this;
}

LocalInstanceGroup::Builder&& LocalInstanceGroup::Builder::AddInstance(
    unsigned id) && {
  AddInstance(id);
  return std::move(*this);
}

LocalInstanceGroup::Builder&& LocalInstanceGroup::Builder::AddInstance(
    unsigned id, std::string name) && {
  AddInstance(id, name);
  return std::move(*this);
}

Result<LocalInstanceGroup> LocalInstanceGroup::Builder::Build() {
  std::vector<std::string> product_out_paths;
  for (size_t i = 0; i < group_proto_.instances_size(); ++i) {
    product_out_paths.emplace_back(ProductDirFromBase(base_dir_, i));
  }

  group_proto_.set_product_out_path(
      android::base::Join(product_out_paths, ","));
  return CF_EXPECT(LocalInstanceGroup::Create(group_proto_));
}

bool LocalInstanceGroup::HasActiveInstances() const {
  for (const auto& instance : instances_) {
    if (instance.IsActive()) {
      return true;
    }
  }
  return false;
}

void LocalInstanceGroup::SetAllStates(cvd::InstanceState state) {
  for (auto& instance : Instances()) {
    instance.set_state(state);
  }
}

TimeStamp LocalInstanceGroup::StartTime() const {
  return CvdServerClock::from_time_t(group_proto_->start_time_sec());
}

void LocalInstanceGroup::SetStartTime(TimeStamp time) {
  group_proto_->set_start_time_sec(CvdServerClock::to_time_t(time));
}

LocalInstanceGroup::LocalInstanceGroup(const cvd::InstanceGroup& group_proto)
    : group_proto_(new cvd::InstanceGroup(group_proto)) {
  for (auto& instance : *group_proto_->mutable_instances()) {
    instances_.push_back(LocalInstance(group_proto_, &instance));
  }
};

Result<LocalInstance> LocalInstanceGroup::FindInstanceById(
    const unsigned id) const {
  for (const auto& instance : instances_) {
    if (instance.id() == id) {
      return instance;
    }
  }
  return CF_ERRF("Group {} has no instance with id {}", GroupName(), id);
}

std::vector<LocalInstance> LocalInstanceGroup::FindByInstanceName(
    const std::string& instance_name) const {
  return Filter(Instances(), [instance_name](const LocalInstance& instance) {
    return instance.name() == instance_name;
  });
}

std::string LocalInstanceGroup::AssemblyDir() const {
  return AssemblyDirFromHome(HomeDir());
}

std::string LocalInstanceGroup::MetricsDir() const {
  return HomeDir() + "/metrics";
}

std::string LocalInstanceGroup::ArtifactsDir() const {
  return BaseDir() + "/artifacts";
}

std::string LocalInstanceGroup::ProductDir(int instance_index) const {
  return fmt::format("{}/{}", ArtifactsDir(), instance_index);
}

std::string LocalInstanceGroup::BaseDir() const {
  // The base directory is always the parent of the home directory
  return android::base::Dirname(HomeDir());
}

Result<Json::Value> LocalInstanceGroup::FetchStatus(
    std::chrono::seconds timeout) {
  Json::Value instances_json(Json::arrayValue);
  for (auto& instance : Instances()) {
    auto instance_status_json = CF_EXPECT(instance.FetchStatus(timeout));
    instances_json.append(instance_status_json);
  }
  Json::Value group_json;
  group_json["group_name"] = GroupName();
  group_json["metrics_dir"] = MetricsDir();
  group_json["start_time"] = Format(StartTime());
  group_json["instances"] = instances_json;
  return group_json;
}

}  // namespace cuttlefish
