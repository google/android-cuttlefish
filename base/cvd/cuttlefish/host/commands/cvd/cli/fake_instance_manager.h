/*
 * Copyright (C) 2026 The Android Open Source Project
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

#pragma once

#include <stddef.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/host/commands/cvd/instances/instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class FakeInstanceManager : public InstanceManager {
 public:
  FakeInstanceManager() : InstanceManager(nullptr, unused_db_, "") {}

  void SetGroups(std::vector<LocalInstanceGroup> groups) {
    groups_ = std::move(groups);
  }
  void OnPrune(std::function<void(int)> callback) {
    on_prune_ = std::move(callback);
  }

  int prune_calls() const { return prune_calls_; }
  int persist_calls() const { return persist_calls_; }

  Result<bool> HasInstanceGroups() const override { return !groups_.empty(); }

  Result<std::vector<LocalInstanceGroup>> FindGroups(
      const InstanceDatabase::Filter& filter) const override {
    std::vector<LocalInstanceGroup> matches;
    for (const LocalInstanceGroup& group : groups_) {
      if (filter.group_name.has_value() &&
          group.GroupName() != *filter.group_name) {
        continue;
      }
      matches.push_back(group.Clone());
    }
    return matches;
  }

  Result<LocalInstanceGroup> FindGroup(
      const InstanceDatabase::Filter& filter) const override {
    std::vector<LocalInstanceGroup> matches = CF_EXPECT(FindGroups(filter));
    CF_EXPECT_EQ(matches.size(), 1ul);
    return std::move(matches.front());
  }

  Result<std::pair<LocalInstance, LocalInstanceGroup>> FindInstanceWithGroup(
      const InstanceDatabase::Filter& filter) const override {
    for (const LocalInstanceGroup& group : groups_) {
      LocalInstanceGroup copy = group.Clone();
      for (const LocalInstance& inst : copy.Instances()) {
        if (filter.instance_id.has_value() &&
            inst.Id() != *filter.instance_id) {
          continue;
        }
        return std::make_pair(inst, std::move(copy));
      }
    }
    return CF_ERR("No matching instance");
  }

  Result<void> PruneBrokenInstanceGroups() override {
    ++prune_calls_;
    if (on_prune_) {
      on_prune_(prune_calls_);
    }
    return {};
  }

  Result<void> PersistInstanceStates(const LocalInstanceGroup& before,
                                     const LocalInstanceGroup& after) override {
    ++persist_calls_;
    for (LocalInstanceGroup& group : groups_) {
      if (group.GroupName() != before.GroupName()) {
        continue;
      }
      for (size_t i = 0;
           i < group.Instances().size() && i < after.Instances().size(); ++i) {
        group.Instances()[i].SetState(after.Instances()[i].State());
      }
    }
    return {};
  }

 private:
  InstanceDatabase unused_db_{""};
  std::vector<LocalInstanceGroup> groups_;
  std::function<void(int)> on_prune_;
  int prune_calls_ = 0;
  int persist_calls_ = 0;
};

inline Result<LocalInstanceGroup> MakeGroup(const std::string& name,
                                            unsigned id,
                                            cvd::InstanceState state) {
  LocalInstanceGroup group =
      CF_EXPECT(LocalInstanceGroup::Builder(name, "/nonexistent/cvd")
                    .AddInstance(id, std::to_string(id))
                    .Build());
  group.SetAllStates(state);
  return group;
}

inline Result<CommandRequest> MakeRequest(
    const std::string& subcmd, const std::vector<std::string>& args = {},
    selector::SelectorOptions selectors = {}) {
  std::vector<std::string> full_args = {"cvd", subcmd};
  full_args.insert(full_args.end(), args.begin(), args.end());
  return CommandRequestBuilder()
      .AddArguments(full_args)
      .SetSelectorOptions(std::move(selectors))
      .Build();
}

}  // namespace cuttlefish
