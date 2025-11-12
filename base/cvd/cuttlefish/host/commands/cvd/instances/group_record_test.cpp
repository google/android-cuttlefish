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
#include "cuttlefish/host/commands/cvd/instances/instance_group_record.h"

namespace cuttlefish {
namespace selector {

static std::string GroupName() { return "yah_ong"; }

class CvdInstanceGroupUnitTest : public testing::Test {
 protected:
  CvdInstanceGroupUnitTest() {}
  Result<LocalInstanceGroup> Get() {
    return LocalInstanceGroup::Create(group_params);
  }
  InstanceGroupParams group_params{
      .group_name = GroupName(),
      .instances =
          {
              {
                  .instance_id = 1,
                  .per_instance_name = "tv_instance",
              },
              {
                  .instance_id = 2,
                  .per_instance_name = "2",
              },
              {
                  .instance_id = 3,
                  .per_instance_name = "phone",
              },
              {
                  .instance_id = 7,
                  .per_instance_name = "tv_instances",
              },
          },
  };
};

TEST_F(CvdInstanceGroupUnitTest, AddInstancesAndListAll) {
  auto group_res = Get();
  auto instances = group_res->Instances();

  cvd::InstanceGroup group;
  ASSERT_EQ(instances.size(), 4);
}

TEST_F(CvdInstanceGroupUnitTest, SearchById) {
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
  // valid_ids were added in the CvdInstanceGroupSearchUnitTest_SearchById
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
