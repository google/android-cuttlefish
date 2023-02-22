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
  CvdInstanceGroupUnitTest() : group_(GroupName(), HomeDir(), TestBinDir()) {}
  LocalInstanceGroup& Get() { return group_; }
  LocalInstanceGroup group_;
};

// CvdInstanceGroupUnitTest + add 4 instances
class CvdInstanceGroupSearchUnitTest : public testing::Test {
 protected:
  CvdInstanceGroupSearchUnitTest()
      : group_(GroupName(), HomeDir(), TestBinDir()) {
    is_setup_ =
        (Get().AddInstance(1, "tv_instance").ok() &&
         Get().AddInstance(2, "2").ok() && Get().AddInstance(3, "phone").ok() &&
         Get().AddInstance(7, "tv_instance").ok());
    is_setup_ = is_setup_ && (Get().Instances().size() == 4);
  }
  LocalInstanceGroup& Get() { return group_; }
  bool IsSetup() const { return is_setup_; }

 private:
  bool is_setup_;
  LocalInstanceGroup group_;
};

TEST_F(CvdInstanceGroupUnitTest, Fields) {
  auto& group = Get();

  ASSERT_EQ(group.InternalGroupName(), "cvd");
  ASSERT_EQ(group.GroupName(), "yah_ong");
  ASSERT_EQ(group.HomeDir(), HomeDir());
  ASSERT_EQ(group.HostArtifactsPath(), TestBinDir());
}

TEST_F(CvdInstanceGroupUnitTest, AddInstances) {
  auto& group = Get();

  ASSERT_TRUE(group.AddInstance(1, "tv_instance").ok());
  ASSERT_TRUE(group.AddInstance(2, "2").ok());
  ASSERT_TRUE(group.AddInstance(3, "phone").ok());
  ASSERT_EQ(group.Instances().size(), 3);
}

TEST_F(CvdInstanceGroupUnitTest, AddInstancesAndListAll) {
  auto& group = Get();
  group.AddInstance(1, "tv_instance");
  group.AddInstance(2, "2");
  group.AddInstance(3, "phone");
  if (group.Instances().size() != 3) {
    GTEST_SKIP() << "AddInstance failed but is verified in other testing.";
  }

  auto set_result = group.FindAllInstances();

  ASSERT_TRUE(set_result.ok()) << set_result.error().Trace();
  ASSERT_EQ(set_result->size(), 3);
}

TEST_F(CvdInstanceGroupSearchUnitTest, SearchById) {
  auto& group = Get();
  if (!IsSetup()) {
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
    auto result = group.FindById(valid_id);
    ASSERT_TRUE(result.ok());
    auto set = *result;
    ASSERT_EQ(set.size(), 1);
    const LocalInstance& instance = *set.cbegin();
    ASSERT_EQ(instance.InstanceId(), valid_id);
  }

  // invalid search
  for (auto const& invalid_id : invalid_ids) {
    auto result = group.FindById(invalid_id);
    // it's okay not to be found
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(result->empty());
  }
}

}  // namespace selector
}  // namespace cuttlefish
