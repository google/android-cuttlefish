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

namespace cuttlefish {
namespace selector {

/**
 * Note that invalid inputs must be tested at the InstanceDatabase level
 */
TEST(CvdInstanceRecordUnitTest, Fields) {
  cvd::InstanceGroup group_proto;
  group_proto.set_name("super");
  group_proto.set_home_directory("/home/user");
  group_proto.set_host_artifacts_path("/home/user/download/bin");
  group_proto.set_product_out_path("/home/user/download/bin");
  auto instance_proto = group_proto.add_instances();
  instance_proto->set_id(3);
  instance_proto->set_name("phone");
  auto parent_group_res = LocalInstanceGroup::Create(group_proto);
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

  ASSERT_EQ(instance.id(), 3);
  ASSERT_EQ(instance.name(), "phone");
  ASSERT_EQ(parent_group.Proto().name(), "super");
  ASSERT_EQ(parent_group.Proto().home_directory(), "/home/user");
  ASSERT_EQ(parent_group.Proto().host_artifacts_path(), "/home/user/download/bin");
  ASSERT_EQ(parent_group.Proto().product_out_path(), "/home/user/download/bin");
}

}  // namespace selector
}  // namespace cuttlefish
