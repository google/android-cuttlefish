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

#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"

namespace cuttlefish {
namespace selector {

static std::string GroupName() { return "yah_ong"; }
static std::string HomeDir() { return "/home/user"; }
static std::string TestBinDir() { return "/opt/android11"; }

class CvdInstanceGroupUnitTest : public testing::Test {
 protected:
  CvdInstanceGroupUnitTest()
      : group_info({.group_name = GroupName(),
                    .home_dir = HomeDir(),
                    .host_artifacts_path = TestBinDir(),
                    .product_out_path = TestBinDir()}) {}
  Result<LocalInstanceGroup> Get() {
    return WithInstances({
        {1, "tv_instance"},
        {2, "2"},
        {3, "phone"},
        {7, "tv_instances"},
    });
  }
  Result<LocalInstanceGroup> WithInstances(
      const std::vector<InstanceInfo>& instances) {
    return LocalInstanceGroup::Create(group_info, instances);
  }
  InstanceGroupInfo group_info;
};

TEST_F(CvdInstanceGroupUnitTest, AddInstancesAndListAll) {
  auto group_res = Get();
  auto instances = group_res->Instances();

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
    auto instances = group_res->FindById(valid_id);
    ASSERT_EQ(instances.size(), 1);
    const LocalInstance& instance = *instances.cbegin();
    ASSERT_EQ(instance.InstanceId(), valid_id);
  }

  // invalid search
  for (auto const& invalid_id : invalid_ids) {
    auto instances = group_res->FindById(invalid_id);
    // it's okay not to be found
    ASSERT_TRUE(instances.empty());
  }
}

}  // namespace selector
}  // namespace cuttlefish
