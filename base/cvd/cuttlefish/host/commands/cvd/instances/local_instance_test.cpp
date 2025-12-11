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

#include <gtest/gtest.h>

#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"

namespace cuttlefish {
namespace selector {

/**
 * Note that invalid inputs must be tested at the InstanceDatabase level
 */
TEST(LocalinstanceTest, Fields) {
  auto parent_group_res =
      LocalInstanceGroup::Builder("super").AddInstance(3, "phone").Build();
  if (!parent_group_res.ok()) {
    /*
     * Here's why we skip the test rather than see it as a failure.
     *
     * 1. The test is specifically designed for operations in
     *    LocalInstanceRecord.
     * 2. Adding instance to a group is tested in another test suites designed
     *    for LocalInstanceGroup. It's a failure there but not here.
     *
     */
    GTEST_SKIP() << "Failed to add instance group. Set up failed.";
  }
  const auto& parent_group = *parent_group_res;
  const auto& instances = parent_group.Instances();
  auto& instance = *instances.cbegin();

  EXPECT_EQ(instance.id(), 3);
  EXPECT_EQ(instance.name(), "phone");
  EXPECT_EQ(parent_group.Proto().name(), "super");
}

}  // namespace selector
}  // namespace cuttlefish
