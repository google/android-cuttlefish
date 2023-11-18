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

#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/instance_record.h"

namespace cuttlefish {
namespace selector {

/**
 * Note that invalid inputs must be tested at the InstanceDatabase level
 */
TEST(CvdInstanceRecordUnitTest, Fields) {
  LocalInstanceGroup parent_group(
      {.group_name = "super",
       .home_dir = "/home/user",
       .host_artifacts_path = "/home/user/download/bin",
       .product_out_path = "/home/user/download/bin"});
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

/**
 * Note that invalid inputs must be tested at the InstanceDatabase level
 */
TEST(CvdInstanceRecordUnitTest, Copy) {
  LocalInstanceGroup parent_group(
      {.group_name = "super",
       .home_dir = "/home/user",
       .host_artifacts_path = "/home/user/download/bin",
       .product_out_path = "/home/user/download/bin"});
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
  auto copy = instance->GetCopy();

  ASSERT_EQ(instance->InstanceId(), copy.InstanceId());
  ASSERT_EQ(instance->InternalName(), copy.InternalName());
  ASSERT_EQ(instance->PerInstanceName(), copy.PerInstanceName());
  ASSERT_EQ(instance->InternalDeviceName(), copy.InternalDeviceName());
  ASSERT_EQ(instance->DeviceName(), copy.DeviceName());
}

}  // namespace selector
}  // namespace cuttlefish
