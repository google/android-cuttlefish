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

#include "host/commands/cvd/instance_group_record.h"
#include "host/commands/cvd/instance_record.h"

namespace cuttlefish {
namespace instance_db {

/**
 * Note that invalid inputs must be tested at the InstanceDatabase level
 */
TEST(CvdInstanceRecordUnitTest, Fields) {
  LocalInstanceGroup parent_group("super", "/home/user",
                                  "/home/user/download/bin");
  if (!parent_group.AddInstance(3, "phone").ok()) {
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
  auto& instances = parent_group.Instances();
  auto& instance = *instances.cbegin();

  ASSERT_EQ(instance->InstanceId(), 3);
  ASSERT_EQ(instance->InternalName(), "3");
  ASSERT_EQ(instance->PerInstanceName(), "phone");
  ASSERT_EQ(instance->InternalDeviceName(), "cvd-3");
  ASSERT_EQ(instance->DeviceName(), "super-phone");
  ASSERT_EQ(std::addressof(instance->ParentGroup()),
            std::addressof(parent_group));
}

}  // namespace instance_db
}  // namespace cuttlefish
