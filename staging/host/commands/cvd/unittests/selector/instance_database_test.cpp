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

#include "common/libs/utils/files.h"
#include "host/commands/cvd/selector/instance_database.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/unittests/selector/instance_database_helper.h"

/*
 * SetUp creates a mock ANDROID_HOST_OUT directory where there is
 * bin/launch_cvd, and a "Workspace" directory where supposedly HOME
 * directories for each LocalInstanceGroup will be populated.
 *
 * InstanceDatabase APIs conduct validity checks: e.g. if the host tool
 * directory actually has host tools such as launch_cvd, if the "HOME"
 * directory for the LocalInstanceGroup is actually an existing directory,
 * and so on.
 *
 * With TEST_F(Suite, Test), the following is the class declaration:
 *  class Suite : public testing::Test;
 *  class Suite_Test : public Suite;
 *
 * Thus, the set up is done inside the constructur of the Suite base class.
 * Also, cleaning up the directories and files are done inside the destructor.
 * If creating files/directories fails, the "Test" is skipped.
 *
 */

namespace cuttlefish {
namespace selector {

TEST_F(CvdInstanceDatabaseTest, Empty) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  ASSERT_TRUE(db.IsEmpty());
  ASSERT_TRUE(db.InstanceGroups().empty());
}

TEST_F(CvdInstanceDatabaseTest, AddWithInvalidGroupInfo) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  // populate home directories under Workspace()
  const std::string home{Workspace() + "/" + "meow"};
  if (!EnsureDirectoryExists(home).ok()) {
    // if ever failed, skip
    GTEST_SKIP() << "Failed to find/create " << home;
  }
  const std::string invalid_host_artifacts_path{Workspace() + "/" + "host_out"};
  if (!EnsureDirectoryExists(invalid_host_artifacts_path).ok() ||
      !EnsureDirectoryExists(invalid_host_artifacts_path + "/bin").ok()) {
    GTEST_SKIP() << "Failed to find/create "
                 << invalid_host_artifacts_path + "/bin";
  }

  // group_name : "meow"
  auto result_bad_home =
      db.AddInstanceGroup("meow", "/path/to/never/exists", HostArtifactsPath());
  auto result_bad_host_bin_dir =
      db.AddInstanceGroup("meow", home, "/path/to/never/exists");
  auto result_both_bad = db.AddInstanceGroup("meow", "/path/to/never/exists",
                                             "/path/to/never/exists");
  auto result_bad_group_name =
      db.AddInstanceGroup("0invalid_group_name", home, HostArtifactsPath());
  // Everything is correct but one thing: the host artifacts directory does not
  // have host tool files such as launch_cvd
  auto result_non_qualifying_host_tool_dir =
      db.AddInstanceGroup("meow", home, invalid_host_artifacts_path);

  ASSERT_FALSE(result_bad_home.ok());
  ASSERT_FALSE(result_bad_host_bin_dir.ok());
  ASSERT_FALSE(result_both_bad.ok());
  ASSERT_FALSE(result_bad_group_name.ok());
  ASSERT_FALSE(result_non_qualifying_host_tool_dir.ok());
}

TEST_F(CvdInstanceDatabaseTest, AddWithValidGroupInfo) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  const std::string home0{Workspace() + "/" + "home0"};
  if (!EnsureDirectoryExists(home0).ok()) {
    GTEST_SKIP() << "Failed to find/create " << home0;
  }
  const std::string home1{Workspace() + "/" + "home1"};
  if (!EnsureDirectoryExists(home1).ok()) {
    GTEST_SKIP() << "Failed to find/create " << home1;
  }

  ASSERT_TRUE(db.AddInstanceGroup("meow", home0, HostArtifactsPath()).ok());
  ASSERT_TRUE(db.AddInstanceGroup("miaou", home1, HostArtifactsPath()).ok());
}

TEST_F(CvdInstanceDatabaseTest, AddToTakenHome) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  const std::string home{Workspace() + "/" + "my_home"};
  if (!EnsureDirectoryExists(home).ok()) {
    GTEST_SKIP() << "Failed to find/create " << home;
  }

  ASSERT_TRUE(db.AddInstanceGroup("meow", home, HostArtifactsPath()).ok());
  ASSERT_FALSE(db.AddInstanceGroup("meow", home, HostArtifactsPath()).ok());
}

TEST_F(CvdInstanceDatabaseTest, Clear) {
  /* AddGroups(name):
   *   HOME: Workspace() + "/" + name
   *   HostArtifactsPath: Workspace() + "/" + "android_host_out"
   *   group_ := LocalInstanceGroup(name, HOME, HostArtifactsPath)
   */
  if (!SetUpOk() || !AddGroups({"nyah", "yah_ong"})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();

  // test Clear()
  ASSERT_FALSE(db.IsEmpty());
  db.Clear();
  ASSERT_TRUE(db.IsEmpty());
}

TEST_F(CvdInstanceDatabaseTest, SearchGroups) {
  if (!SetUpOk() || !AddGroups({"myau", "miau"})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  const std::string valid_home_search_key{Workspace() + "/" + "myau"};
  const std::string invalid_home_search_key{"/no/such/path"};

  auto valid_groups = db.FindGroups({kHomeField, valid_home_search_key});
  auto valid_group = db.FindGroup({kHomeField, valid_home_search_key});
  auto invalid_groups = db.FindGroups({kHomeField, invalid_home_search_key});
  auto invalid_group = db.FindGroup({kHomeField, invalid_home_search_key});

  ASSERT_TRUE(valid_groups.ok());
  ASSERT_EQ(valid_groups->size(), 1);
  ASSERT_TRUE(valid_group.ok());

  ASSERT_TRUE(invalid_groups.ok());
  ASSERT_EQ(invalid_groups->size(), 0);
  ASSERT_FALSE(invalid_group.ok());
}

TEST_F(CvdInstanceDatabaseTest, RemoveGroup) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  if (!AddGroups({"miaaaw", "meow", "mjau"})) {
    GTEST_SKIP() << Error().msg;
  }
  auto eng_group = db.FindGroup({kHomeField, Workspace() + "/" + "meow"});
  if (!eng_group.ok()) {
    GTEST_SKIP() << "meow"
                 << " group was not found.";
  }

  ASSERT_TRUE(db.RemoveInstanceGroup(*eng_group));
  ASSERT_FALSE(db.RemoveInstanceGroup(*eng_group));
}

TEST_F(CvdInstanceDatabaseTest, AddInstances) {
  if (!SetUpOk() || !AddGroups({"yah_ong"})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  auto kitty_group = db.FindGroup({kHomeField, Workspace() + "/" + "yah_ong"});
  if (!kitty_group.ok()) {
    GTEST_SKIP() << "yah_ong"
                 << " group was not found";
  }
  const auto& instances = kitty_group->Get().Instances();

  ASSERT_TRUE(db.AddInstance(*kitty_group, 1, "yumi").ok());
  ASSERT_FALSE(db.AddInstance(*kitty_group, 3, "yumi").ok());
  ASSERT_FALSE(db.AddInstance(*kitty_group, 1, "tiger").ok());
  ASSERT_TRUE(db.AddInstance(*kitty_group, 3, "tiger").ok());
  for (auto const& instance_unique_ptr : instances) {
    ASSERT_TRUE(instance_unique_ptr->PerInstanceName() == "yumi" ||
                instance_unique_ptr->PerInstanceName() == "tiger");
  }
}

TEST_F(CvdInstanceDatabaseTest, AddInstancesInvalid) {
  if (!SetUpOk() || !AddGroups({"yah_ong"})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  auto kitty_group = db.FindGroup({kHomeField, Workspace() + "/" + "yah_ong"});
  if (!kitty_group.ok()) {
    GTEST_SKIP() << "yah_ong"
                 << " group was not found";
  }

  ASSERT_FALSE(db.AddInstance(*kitty_group, 1, "!yumi").ok());
  ASSERT_FALSE(db.AddInstance(*kitty_group, 7, "ti ger").ok());
}

TEST_F(CvdInstanceDatabaseTest, FindByInstanceId) {
  // The start of set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroups({"miau", "nyah"})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  // per_instance_name could be the same if the parent groups are different.
  std::vector<InstanceInfo> miau_group_instance_id_name_pairs{
      {1, "8"}, {10, "tv-instance"}};
  std::vector<InstanceInfo> nyah_group_instance_id_name_pairs{
      {7, "my_favorite_phone"}, {11, "tv-instance"}, {3, "3_"}};
  auto miau_group = db.FindGroup({kHomeField, Workspace() + "/" + "miau"});
  auto nyah_group = db.FindGroup({kHomeField, Workspace() + "/" + "nyah"});
  if (!miau_group.ok() || !nyah_group.ok()) {
    GTEST_SKIP() << "miau or nyah group"
                 << " group was not found";
  }
  if (!AddInstances(*miau_group, miau_group_instance_id_name_pairs) ||
      !AddInstances(*nyah_group, nyah_group_instance_id_name_pairs)) {
    GTEST_SKIP() << Error().msg;
  }
  // The end of set up

  auto result1 = db.FindInstance({kInstanceIdField, std::to_string(1)});
  auto result10 = db.FindInstance({kInstanceIdField, std::to_string(10)});
  auto result7 = db.FindInstance({kInstanceIdField, std::to_string(7)});
  auto result11 = db.FindInstance({kInstanceIdField, std::to_string(11)});
  auto result3 = db.FindInstance({kInstanceIdField, std::to_string(3)});
  auto result_invalid = db.FindInstance({kInstanceIdField, std::to_string(20)});

  ASSERT_TRUE(result1.ok());
  ASSERT_TRUE(result10.ok());
  ASSERT_TRUE(result7.ok());
  ASSERT_TRUE(result11.ok());
  ASSERT_TRUE(result3.ok());
  ASSERT_EQ(result1->Get().PerInstanceName(), "8");
  ASSERT_EQ(result10->Get().PerInstanceName(), "tv-instance");
  ASSERT_EQ(result7->Get().PerInstanceName(), "my_favorite_phone");
  ASSERT_EQ(result11->Get().PerInstanceName(), "tv-instance");
  ASSERT_EQ(result3->Get().PerInstanceName(), "3_");
  ASSERT_FALSE(result_invalid.ok());
}

TEST_F(CvdInstanceDatabaseTest, FindByPerInstanceName) {
  // starting set up
  if (!SetUpOk() || !AddGroups({"miau", "nyah"})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  std::vector<InstanceInfo> miau_group_instance_id_name_pairs{
      {1, "8"}, {10, "tv_instance"}};
  std::vector<InstanceInfo> nyah_group_instance_id_name_pairs{
      {7, "my_favorite_phone"}, {11, "tv_instance"}};
  auto miau_group = db.FindGroup({kHomeField, Workspace() + "/" + "miau"});
  auto nyah_group = db.FindGroup({kHomeField, Workspace() + "/" + "nyah"});
  if (!miau_group.ok() || !nyah_group.ok()) {
    GTEST_SKIP() << "miau or nyah "
                 << " group was not found";
  }
  if (!AddInstances(*miau_group, miau_group_instance_id_name_pairs) ||
      !AddInstances(*nyah_group, nyah_group_instance_id_name_pairs)) {
    GTEST_SKIP() << Error().msg;
  }
  // end of set up

  auto result1 = db.FindInstance({kInstanceNameField, "8"});
  auto result10_and_11 = db.FindInstances({kInstanceNameField, "tv_instance"});
  auto result7 = db.FindInstance({kInstanceNameField, "my_favorite_phone"});
  auto result_invalid =
      db.FindInstance({kInstanceNameField, "name_never_seen"});

  ASSERT_TRUE(result1.ok());
  ASSERT_TRUE(result10_and_11.ok());
  ASSERT_TRUE(result7.ok());
  ASSERT_EQ(result10_and_11->size(), 2);
  ASSERT_EQ(result1->Get().InstanceId(), 1);
  ASSERT_EQ(result7->Get().InstanceId(), 7);
  ASSERT_FALSE(result_invalid.ok());
}

TEST_F(CvdInstanceDatabaseTest, FindInstancesByGroupName) {
  // starting set up
  if (!SetUpOk() || !AddGroups({"miau", "nyah"})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  std::vector<InstanceInfo> nyah_group_instance_id_name_pairs{
      {7, "my_favorite_phone"}, {11, "tv_instance"}};
  auto nyah_group = db.FindGroup({kHomeField, Workspace() + "/" + "nyah"});
  if (!nyah_group.ok()) {
    GTEST_SKIP() << "nyah group was not found";
  }
  if (!AddInstances(*nyah_group, nyah_group_instance_id_name_pairs)) {
    GTEST_SKIP() << Error().msg;
  }
  // end of set up

  auto result_nyah = db.FindInstances({kGroupNameField, "nyah"});
  auto result_invalid = db.FindInstance({kGroupNameField, "name_never_seen"});

  ASSERT_TRUE(result_nyah.ok());
  std::set<std::string> nyah_instance_names;
  for (const auto& instance : *result_nyah) {
    nyah_instance_names.insert(instance.Get().PerInstanceName());
  }
  std::set<std::string> expected{"my_favorite_phone", "tv_instance"};
  ASSERT_EQ(nyah_instance_names, expected);
  ASSERT_FALSE(result_invalid.ok());
}

TEST_F(CvdInstanceDatabaseTest, FindGroupByPerInstanceName) {
  // starting set up
  if (!SetUpOk() || !AddGroups({"miau", "nyah"})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  std::vector<InstanceInfo> miau_group_instance_id_name_pairs{
      {1, "8"}, {10, "tv_instance"}};
  std::vector<InstanceInfo> nyah_group_instance_id_name_pairs{
      {7, "my_favorite_phone"}, {11, "tv_instance"}};
  auto miau_group = db.FindGroup({kHomeField, Workspace() + "/" + "miau"});
  auto nyah_group = db.FindGroup({kHomeField, Workspace() + "/" + "nyah"});
  if (!miau_group.ok() || !nyah_group.ok()) {
    GTEST_SKIP() << "miau or nyah "
                 << " group was not found";
  }
  if (!AddInstances(*miau_group, miau_group_instance_id_name_pairs) ||
      !AddInstances(*nyah_group, nyah_group_instance_id_name_pairs)) {
    GTEST_SKIP() << Error().msg;
  }
  // end of set up

  auto result_miau = db.FindGroups({kInstanceNameField, "8"});
  auto result_both = db.FindGroups({kInstanceNameField, "tv_instance"});
  auto result_nyah = db.FindGroups({kInstanceNameField, "my_favorite_phone"});
  auto result_invalid = db.FindGroups({kInstanceNameField, "name_never_seen"});

  ASSERT_TRUE(result_miau.ok());
  ASSERT_TRUE(result_both.ok());
  ASSERT_TRUE(result_nyah.ok());
  ASSERT_TRUE(result_invalid.ok());
  ASSERT_EQ(result_miau->size(), 1);
  ASSERT_EQ(result_both->size(), 2);
  ASSERT_EQ(result_nyah->size(), 1);
  ASSERT_TRUE(result_invalid->empty())
      << "result_invalid should be empty but with size: "
      << result_invalid->size();
}

}  // namespace selector
}  // namespace cuttlefish
