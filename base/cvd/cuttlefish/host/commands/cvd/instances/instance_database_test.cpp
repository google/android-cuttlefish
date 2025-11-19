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

#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/common/libs/utils/result_matchers.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database.h"
#include "cuttlefish/host/commands/cvd/instances/instance_database_helper.h"
#include "cuttlefish/host/commands/cvd/instances/instance_group_record.h"

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
 * Thus, the set up is done inside the constructor of the Suite base class.
 * Also, cleaning up the directories and files are done inside the destructor.
 * If creating files/directories fails, the "Test" is skipped.
 *
 */

namespace cuttlefish {
namespace selector {

LocalInstanceGroup::Builder GroupParamWithInstances(
    const std::string& name, const std::string& home_dir,
    const std::string& host_path,
    const std::vector<std::optional<std::string>>& product_paths,
    const std::vector<std::pair<unsigned, std::string>>& instances) {
  LocalInstanceGroup::Builder builder(name);
  for (const auto& pair : instances) {
    builder.AddInstance(pair.first, pair.second);
  }
  return builder;
}

TEST_F(CvdInstanceDatabaseTest, Empty) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  ASSERT_THAT(db.IsEmpty(), IsOkAndValue(true));
  auto group_res = db.InstanceGroups();
  ASSERT_TRUE(group_res.ok() && (*group_res).empty());
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

  auto group_builder1 =
      GroupParamWithInstances("0invalid_group_name", home, HostArtifactsPath(),
                              {HostArtifactsPath()}, {{2, "name"}});
  auto result_bad_group_name =
      db.AddInstanceGroup(group_builder1.Build().value());

  // Everything is correct but one thing: the host artifacts directory does not
  // have host tool files such as launch_cvd
  auto group_builder2 =
      GroupParamWithInstances("0invalid_group_name", home, HostArtifactsPath(),
                              {HostArtifactsPath()}, {{2, "name"}});
  auto result_non_qualifying_host_tool_dir =
      db.AddInstanceGroup(group_builder2.Build().value());

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

  auto group_builder1 = GroupParamWithInstances(
      "meow", home0, HostArtifactsPath(), {HostArtifactsPath()}, {{1, "name"}});
  ASSERT_TRUE(db.AddInstanceGroup(group_builder1.Build().value()).ok());

  auto group_builder2 =
      GroupParamWithInstances("miaou", home1, HostArtifactsPath(),
                              {HostArtifactsPath()}, {{2, "name"}});
  ASSERT_TRUE(db.AddInstanceGroup(group_builder2.Build().value()).ok());
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

  auto group_builder1 = GroupParamWithInstances(
      "meow", home, HostArtifactsPath(), {HostArtifactsPath()}, {{1, "name"}});
  ASSERT_TRUE(db.AddInstanceGroup(group_builder1.Build().value()).ok());
  auto group_builder2 = GroupParamWithInstances(
      "meow", home, HostArtifactsPath(), {HostArtifactsPath()}, {{2, "name"}});
  ASSERT_FALSE(db.AddInstanceGroup(group_builder2.Build().value()).ok());
}

TEST_F(CvdInstanceDatabaseTest, Clear) {
  /* AddGroups(name):
   *   HOME: Workspace() + "/" + name
   *   HostArtifactsPath: Workspace() + "/" + "android_host_out"
   *   group_ := LocalInstanceGroup(name, HOME, HostArtifactsPath)
   */
  if (!SetUpOk() || !AddGroup("nyah", {{1, "name"}}) ||
      !AddGroup("yah_ong", {{2, "name"}})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();

  // test Clear()
  ASSERT_THAT(db.IsEmpty(), IsOkAndValue(false));
  ASSERT_TRUE(db.Clear().ok());
  ASSERT_THAT(db.IsEmpty(), IsOkAndValue(true));
}

TEST_F(CvdInstanceDatabaseTest, SearchGroups) {
  if (!SetUpOk() || !AddGroup("myau", {{1, "name"}}) ||
      !AddGroup("miau", {{2, "name"}})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  const std::string valid_group_name{"myau"};
  const std::string invalid_group_name{"nosuchgroup"};

  auto valid_groups = db.FindGroups({.group_name = valid_group_name});
  auto valid_group = db.FindGroup({.group_name = valid_group_name});
  auto invalid_groups = db.FindGroups({.group_name = invalid_group_name});
  auto invalid_group = db.FindGroup({.group_name = invalid_group_name});

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
  if (!AddGroup("miaaaw", {{1, "name"}}) || !AddGroup("meow", {{2, "name"}}) ||
      !AddGroup("mjau", {{3, "name"}})) {
    GTEST_SKIP() << Error().msg;
  }
  auto eng_group = db.FindGroup({.group_name = "meow"});
  if (!eng_group.ok()) {
    GTEST_SKIP() << "meow"
                 << " group was not found.";
  }

  ASSERT_THAT(db.RemoveInstanceGroup(eng_group->GroupName()),
              IsOkAndValue(true));
  ASSERT_THAT(db.RemoveInstanceGroup(eng_group->GroupName()),
              IsOkAndValue(false));
}

TEST_F(CvdInstanceDatabaseTest, AddInstances) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  ASSERT_TRUE(AddGroup({"yah_ong1"}, {{1, "yumi"}, {2, "tiger"}}));
  ASSERT_FALSE(AddGroup({"yah_ong2"}, {{3, "yumi"}, {4, "yumi"}}));
  ASSERT_FALSE(AddGroup({"yah_ong3"}, {{5, "yumi"}, {5, "tiger"}}));
  ASSERT_FALSE(AddGroup({"yah_ong4"}, {{1, "yumi"}, {6, "tiger"}}));
  auto kitty_group = db.FindGroup({.group_name = "yah_ong1"});
  if (!kitty_group.ok()) {
    GTEST_SKIP() << "yah_ong1"
                 << " group was not found";
  }
  const auto& instances = kitty_group->Instances();
  for (auto const& instance : instances) {
    ASSERT_TRUE(instance.name() == "yumi" || instance.name() == "tiger");
  }
}

TEST_F(CvdInstanceDatabaseTest, AddInstancesInvalid) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  ASSERT_FALSE(AddGroup("yah_ong", {{1, "!yumi"}}));
  ASSERT_FALSE(AddGroup("yah_ong2", {{2, "ti ger"}}));
}

TEST_F(CvdInstanceDatabaseTest, FindByInstanceId) {
  // The start of set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("miau", {{1, "8"}, {10, "tv-instance"}})) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("nyah",
                {{7, "my_favorite_phone"}, {11, "tv-instance"}, {3, "3_"}})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  auto miau_group = db.FindGroup({.group_name = "miau"});
  auto nyah_group = db.FindGroup({.group_name = "nyah"});
  if (!miau_group.ok() || !nyah_group.ok()) {
    GTEST_SKIP() << "miau or nyah group"
                 << " group was not found";
  }
  // The end of set up

  auto result1 = db.FindInstanceWithGroup({.instance_id = 1});
  auto result10 = db.FindInstanceWithGroup({.instance_id = 10});
  auto result7 = db.FindInstanceWithGroup({.instance_id = 7});
  auto result11 = db.FindInstanceWithGroup({.instance_id = 11});
  auto result3 = db.FindInstanceWithGroup({.instance_id = 3});
  auto result_invalid = db.FindInstanceWithGroup({.instance_id = 20});

  ASSERT_TRUE(result1.ok());
  ASSERT_TRUE(result10.ok());
  ASSERT_TRUE(result7.ok());
  ASSERT_TRUE(result11.ok());
  ASSERT_TRUE(result3.ok());
  ASSERT_EQ(result1->first.name(), "8");
  ASSERT_EQ(result10->first.name(), "tv-instance");
  ASSERT_EQ(result7->first.name(), "my_favorite_phone");
  ASSERT_EQ(result11->first.name(), "tv-instance");
  ASSERT_EQ(result3->first.name(), "3_");
  ASSERT_FALSE(result_invalid.ok());
}

TEST_F(CvdInstanceDatabaseTest, FindByPerInstanceName) {
  // starting set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("miau", {{1, "8"}, {10, "tv_instance"}})) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("nyah", {{7, "my_favorite_phone"}, {11, "tv_instance"}})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  auto miau_group = db.FindGroup({.group_name = "miau"});
  auto nyah_group = db.FindGroup({.group_name = "nyah"});
  if (!miau_group.ok() || !nyah_group.ok()) {
    GTEST_SKIP() << "miau or nyah "
                 << " group was not found";
  }
  // end of set up

  auto result1 = db.FindInstanceWithGroup({.instance_names = {"8"}});
  auto result7 =
      db.FindInstanceWithGroup({.instance_names = {"my_favorite_phone"}});
  auto result_invalid =
      db.FindInstanceWithGroup({.instance_names = {"name_never_seen"}});

  ASSERT_TRUE(result1.ok());
  ASSERT_TRUE(result7.ok());
  ASSERT_EQ(result1->first.id(), 1);
  ASSERT_EQ(result7->first.id(), 7);
  ASSERT_FALSE(result_invalid.ok());
}

TEST_F(CvdInstanceDatabaseTest, FindGroupByPerInstanceName) {
  // starting set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("miau", {{1, "8"}, {10, "tv_instance"}})) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("nyah", {{7, "my_favorite_phone"}, {11, "tv_instance"}})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  auto miau_group = db.FindGroup({.group_name = "miau"});
  auto nyah_group = db.FindGroup({.group_name = "nyah"});
  if (!miau_group.ok() || !nyah_group.ok()) {
    GTEST_SKIP() << "miau or nyah "
                 << " group was not found";
  }
  // end of set up

  auto result_miau = db.FindGroups({.instance_names = {"8"}});
  auto result_both = db.FindGroups({.instance_names = {"tv_instance"}});
  auto result_nyah = db.FindGroups({.instance_names = {"my_favorite_phone"}});
  auto result_invalid = db.FindGroups({.instance_names = {"name_never_seen"}});

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

TEST_F(CvdInstanceDatabaseTest, AddInstancesTogether) {
  // starting set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();

  ASSERT_TRUE(AddGroup("miau", {{1, "8"}, {10, "tv_instance"}}));

  auto result_8 = db.FindInstanceWithGroup({.instance_names = {"8"}});
  auto result_tv =
      db.FindInstanceWithGroup({.instance_names = {"tv_instance"}});

  ASSERT_TRUE(result_8.ok()) << result_8.error().Trace();
  ASSERT_TRUE(result_tv.ok()) << result_tv.error().Trace();
}

TEST_F(CvdInstanceDatabaseTest, UpdateInstances) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();

  LocalInstanceGroup::Builder builder("grp1");
  builder.AddInstance(1, "ins1");
  builder.AddInstance(2, "ins2");

  Result<LocalInstanceGroup> group_res = builder.Build();
  ASSERT_THAT(group_res, IsOk());
  auto add_res = db.AddInstanceGroup(*group_res);
  ASSERT_TRUE(add_res.ok())
      << "Failed to add group to db: " << add_res.error().Message();

  auto instance_group = *(std::move(group_res));
  auto& instance1 = instance_group.Instances()[0];
  instance1.set_state(cvd::INSTANCE_STATE_STARTING);
  auto& instance2 = instance_group.Instances()[1];
  instance2.set_state(cvd::INSTANCE_STATE_STARTING);

  auto update_res = db.UpdateInstanceGroup(instance_group);
  ASSERT_TRUE(update_res.ok())
      << "Failed to update database: " << update_res.error().Message();

  auto find_res = db.FindGroup({.group_name = "grp1"});
  ASSERT_TRUE(find_res.ok()) << find_res.error().Message();

  EXPECT_EQ(find_res->Instances()[0].id(), 1);
  EXPECT_EQ(find_res->Instances()[1].id(), 2);
  EXPECT_EQ(find_res->Instances()[0].state(), cvd::INSTANCE_STATE_STARTING);
  EXPECT_EQ(find_res->Instances()[1].state(), cvd::INSTANCE_STATE_STARTING);
}

}  // namespace selector
}  // namespace cuttlefish
