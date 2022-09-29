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

#include <android-base/file.h>
#include <gtest/gtest.h>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "host/commands/cvd/instance_database_utils.h"
#include "host/commands/cvd/instance_group_record.h"

using LocalInstance = cuttlefish::instance_db::LocalInstance;
using LocalInstanceGroup = cuttlefish::instance_db::LocalInstanceGroup;

namespace {

const std::string TestHome() { return cuttlefish::StringFromEnv("HOME", ""); }

const std::string TestBinDir() {
  return cuttlefish::StringFromEnv("ANDROID_HOST_OUT", ".");
}

LocalInstanceGroup Get(const std::string& home_dir,
                       const std::string& host_binaries_dir) {
  LocalInstanceGroup group(home_dir, host_binaries_dir);
  return group;
}

LocalInstanceGroup Get() {
  const std::string home = TestHome();
  const std::string host_binaries_dir = TestBinDir();
  return Get(home, host_binaries_dir);
}

}  // namespace

TEST(CvdInstanceGroupUnitTest, OperatorEQ) {
  auto group = Get();
  ASSERT_EQ(group, Get());
  ASSERT_FALSE(group == Get(TestHome(), "/tmp/host_bin/placeholder"));
  ASSERT_FALSE(group == Get("/home/placeholder", TestBinDir()));
  ASSERT_FALSE(group == Get("/home/placeholder", "/tmp/host_bin/placeholder"));
}

TEST(CvdInstanceGroupUnitTest, Fields) {
  auto group = Get();
  ASSERT_EQ(group.InternalGroupName(), "cvd");
  ASSERT_EQ(group.HomeDir(), TestHome());
  ASSERT_EQ(group.HostBinariesDir(), TestBinDir());
  std::string home_dir;
  android::base::Realpath(TestHome(), &home_dir);
  auto config_path_result = group.GetCuttlefishConfigPath();
  if (config_path_result.ok()) {
    ASSERT_EQ(*config_path_result,
              home_dir + "/cuttlefish_assembly/cuttlefish_config.json");
  }
}

TEST(CvdInstanceGroupUnitTest, Instances) {
  auto group = Get();
  ASSERT_FALSE(group.HasInstance(1));
  group.AddInstance(1, "tv_instance");
  ASSERT_TRUE(group.HasInstance(1));
  std::vector<int> more_instances{2, 3, 4};
  for (auto const i : more_instances) {
    group.AddInstance(i, std::to_string(i));
  }
  std::vector<int> instances{more_instances};
  instances.emplace_back(1);
  for (auto const i : more_instances) {
    ASSERT_TRUE(group.HasInstance(i));
    auto result = group.FindById(i);
    ASSERT_TRUE(result.ok());
    cuttlefish::instance_db::Set<LocalInstance> set = *result;
    ASSERT_TRUE(cuttlefish::instance_db::AtMostOne(set, "").ok());
  }
  // correct keys
  std::vector<std::string> instance_name_keys = {"tv_instance", "2", "3", "4"};
  for (const auto& key : instance_name_keys) {
    auto result = group.FindByInstanceName(key);
    ASSERT_TRUE(result.ok());
    auto found_instances = *result;
    const auto& instance = *found_instances.cbegin();
    ASSERT_EQ(instance.PerInstanceName(), key);
    ASSERT_EQ(instance.DeviceName(), group.InternalGroupName() + "-" + key);
  }

  // wrong keys
  instance_name_keys = std::vector<std::string>{"phone-instance", "6", ""};
  for (const auto& key : instance_name_keys) {
    auto result = group.FindByInstanceName(key);
    ASSERT_TRUE(result.ok());
    auto subset = *result;
    ASSERT_TRUE(subset.empty());
  }
}
