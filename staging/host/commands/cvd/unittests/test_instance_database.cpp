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

#include <algorithm>
#include <unordered_set>

#include <gtest/gtest.h>

#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/unittests/instance_database_test_helper.h"

using Tester = cuttlefish::instance_db::unittest::DbTester;

TEST(CvdInstanceDatabaseTest, EmptyAddClear) {
  auto tester = Tester();
  auto& db = tester.Db();
  ASSERT_TRUE(db.IsEmpty());
  ASSERT_TRUE(db.InstanceGroups().empty());
  const auto& homes = tester.Homes();
  for (const auto h : homes) {
    auto result = db.AddInstanceGroup(h, tester.HostOutDir() + "/bin");
    ASSERT_TRUE(result.ok());
    ASSERT_FALSE(db.IsEmpty());
  }
  ASSERT_FALSE(db.InstanceGroups().empty());
  db.Clear();
  ASSERT_TRUE(db.IsEmpty());
  ASSERT_TRUE(db.InstanceGroups().empty());
}

TEST(CvdInstanceDatabaseTest, EmptyInstanceGroups) {
  auto tester = Tester();

  auto& db = tester.Db();
  auto&& homes = tester.Homes();
  for (const auto h : homes) {
    auto result = db.AddInstanceGroup(h, tester.HostOutDir() + "/bin");
    EXPECT_TRUE(result.ok());
  }

  for (const auto h : homes) {
    using namespace cuttlefish::selector;
    auto result = db.FindGroups({kHomeField, h});
    ASSERT_TRUE(result.ok());
    auto& group_set = *result;
    ASSERT_TRUE(group_set.size() == 1);
    auto& group = *group_set.cbegin();
    ASSERT_TRUE(group.HomeDir() == h);
    ASSERT_TRUE(db.FindGroup({kHomeField, h}).ok());

    db.RemoveInstanceGroup(group);
    auto result_after_removal = db.FindGroups({kHomeField, h});
    ASSERT_TRUE(result_after_removal.ok() && (*result_after_removal).empty());
    ASSERT_FALSE(db.FindGroup({kHomeField, h}).ok());
  }
}

TEST(CvdInstanceDatabaseTest, InstanceAddAndFind) {
  auto tester = Tester();
  auto& db = tester.Db();
  auto&& homes = tester.Homes();
  const auto n_groups = std::min<unsigned>(5, tester.kNGroups);
  for (int i = 0; i < n_groups; i++) {
    auto r = db.AddInstanceGroup(homes[i], tester.HostOutDir() + "/bin");
    ASSERT_TRUE(r.ok());
  }

  auto testing_instance_names = tester.InstanceNames(n_groups);
  unsigned instance_id = 0;
  auto&& instance_groups = db.InstanceGroups();
  ASSERT_TRUE(testing_instance_names.size() == n_groups &&
              instance_groups.size() == n_groups);

  // add testing_instance_names[g] to instance_groups[g]
  for (int g = 0; g < n_groups; g++) {
    std::vector<std::string> name_set{testing_instance_names[g].begin(),
                                      testing_instance_names[g].end()};

    for (int i = 0; i < name_set.size(); i++) {
      instance_id++;  // starting from 1
      auto r = db.AddInstance(instance_groups[g], instance_id, name_set[i]);
      ASSERT_TRUE(r.ok());
      // try very big number, and fail
      auto r2 =
          db.AddInstance(instance_groups[g], instance_id + 1000, name_set[i]);
      ASSERT_FALSE(r2.ok());
      // try an instance name that doesn't exist
      // note that Tester makes id + 6 long names internally
      auto r3 = db.AddInstance(instance_groups[g], instance_id,
                               name_set[i] + "different_name");
      ASSERT_FALSE(r3.ok());
    }
  }

  // check each group
  for (int g = 0; g < n_groups; g++) {
    auto&& instances = instance_groups[g].Instances();
    ASSERT_TRUE(testing_instance_names[g].size() == instances.size());
    std::unordered_set<std::string> names{testing_instance_names[g].begin(),
                                          testing_instance_names[g].end()};
    for (const auto& instance : instances) {
      ASSERT_TRUE(names.find(instance.PerInstanceName()) != names.end());
    }
  }

  // FindById
  for (int j = 1; j <= instance_id; j++) {
    using namespace cuttlefish::selector;
    auto r = db.FindInstance({kInstanceIdField, std::to_string(j)});
    ASSERT_TRUE(r.ok());
    auto& instance = *r;
    ASSERT_TRUE(instance.InstanceId() == j);
    auto r2 = db.FindInstance({kInstanceIdField, std::to_string(j + 100)});
    ASSERT_FALSE(r2.ok());
  }

  // FindByNames
  for (const auto& names : testing_instance_names) {
    for (const auto& name : names) {
      using namespace cuttlefish::selector;
      auto r = db.FindInstance({kInstanceNameField, name});
      ASSERT_TRUE(r.ok());
      ASSERT_EQ(name, (*r).PerInstanceName());
    }
  }

  {
    using namespace cuttlefish::selector;
    ASSERT_FALSE(db.FindInstance({kInstanceNameField, ""}).ok());
    ASSERT_FALSE(db.FindInstance({kInstanceNameField, "never-exists"}).ok());
  }
}
