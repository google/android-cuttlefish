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

#include "host/commands/cvd/instance_record.h"

using LocalInstance = cuttlefish::instance_db::LocalInstance;

/**
 * returns default test instance
 *
 * Note that, as the fields of InstanceRecord are being added,
 * using this function would minimize the line of codes that should
 * be modified when fields are added.
 */
LocalInstance GetInstance() {
  LocalInstance instance(3, "cvd", "cool_group", "phone");
  return instance;
}

/**
 * Note that invalid inputs must be tested at the InstanceDatabase level
 */

TEST(CvdInstanceRecordUnitTest, OperatorEQ) {
  auto instance = GetInstance();
  ASSERT_EQ(instance, LocalInstance(3, "cvd", "cool_group", "phone"));
  ASSERT_FALSE(instance == LocalInstance(7, "cvd", "cool_group", "phone"));
  ASSERT_FALSE(instance == LocalInstance(3, "cvd2", "cool_group", "phone"));
  ASSERT_FALSE(instance == LocalInstance(3, "cvd", "", "phone"));
  ASSERT_FALSE(instance == LocalInstance(3, "cvd", "cool_group", "tv"));
}

TEST(CvdInstanceRecordUnitTest, Fields) {
  auto instance = GetInstance();
  ASSERT_EQ(instance.InstanceId(), 3);
  ASSERT_EQ(instance.InternalName(), "3");
  ASSERT_EQ(instance.PerInstanceName(), "phone");
  ASSERT_EQ(instance.InternalDeviceName(), "cvd-3");
  ASSERT_EQ(instance.DeviceName(), "cool_group-phone");
}
