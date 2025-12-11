//
// Copyright (C) 2022 The Android Open Source Project
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

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cuttlefish/common/libs/utils/result_matchers.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"

namespace cuttlefish {
namespace selector {

static std::string GroupName() { return "yah_ong"; }

class LocalInstanceGroupUnitTest : public testing::Test {
 protected:
  LocalInstanceGroupUnitTest() {}
  Result<LocalInstanceGroup> Get() {
    LocalInstanceGroup::Builder builder(GroupName());
    builder.AddInstance(1, "tv_instance");
    builder.AddInstance(2, "2");
    builder.AddInstance(3, "phone");
    builder.AddInstance(7, "tv_instances");
    return builder.Build();
  }
};

TEST_F(LocalInstanceGroupUnitTest, AddInstancesAndListAll) {
  auto group_res = Get();
  auto instances = group_res->Instances();

  cvd::InstanceGroup group;
  ASSERT_EQ(instances.size(), 4);
}

TEST_F(LocalInstanceGroupUnitTest, SearchById) {
  auto group_res = Get();
  if (!group_res.ok()) {
    /*
     * Here's why we skip the test rather than see it as a failure.
     *
     * 1. The test is specifically designed for searcy-by-id operations.
     * 2. Adding instance to a group is tested in AddInstances test designed
     *    specifically for it. It's a failure there but not here.
     */
    GTEST_SKIP() << "Failed to add instances to the group.";
  }
  // valid_ids were added in the LocalinstanceGroupUnitTest_SearchById
  // constructor.
  std::vector<unsigned> valid_ids{1, 2, 7};
  std::vector<unsigned> invalid_ids{20, 0, 5};

  // valid search
  for (auto const& valid_id : valid_ids) {
    auto instance_res = group_res->FindInstanceById(valid_id);
    ASSERT_THAT(instance_res, IsOk());
    auto& instance = *instance_res;
    ASSERT_EQ(instance.id(), valid_id);
  }

  // invalid search
  for (auto const& invalid_id : invalid_ids) {
    auto instance_res = group_res->FindInstanceById(invalid_id);
    // it's okay not to be found
    ASSERT_THAT(instance_res, IsError());
  }
}

}  // namespace selector
}  // namespace cuttlefish
