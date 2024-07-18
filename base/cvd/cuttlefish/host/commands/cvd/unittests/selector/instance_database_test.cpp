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

#include <gtest/gtest.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result_matchers.h"
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
 * Thus, the set up is done inside the constructor of the Suite base class.
 * Also, cleaning up the directories and files are done inside the destructor.
 * If creating files/directories fails, the "Test" is skipped.
 *
 */

namespace cuttlefish {
namespace selector {

cvd::Instance InstanceProto(unsigned id, const std::string& name) {
  cvd::Instance instance;
  instance.set_id(id);
  instance.set_name(name);
  return instance;
}

cvd::InstanceGroup GroupProtoWithInstances(
    const std::string& name, const std::string& home_dir,
    const std::string& host_path, const std::string& product_path,
    const std::vector<std::pair<unsigned, std::string>>& instances) {
  cvd::InstanceGroup group;
  group.set_name(name);
  group.set_home_directory(home_dir);
  group.set_host_artifacts_path(host_path);
  group.set_product_out_path(product_path);
  for (const auto& pair : instances) {
    *group.add_instances() = InstanceProto(pair.first, pair.second);
  }
  return group;
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

  auto group_proto1 =
      GroupProtoWithInstances("0invalid_group_name", home, HostArtifactsPath(),
                              HostArtifactsPath(), {{2, "name"}});
  auto result_bad_group_name = db.AddInstanceGroup(group_proto1);

  // Everything is correct but one thing: the host artifacts directory does not
  // have host tool files such as launch_cvd
  auto group_proto2 =
      GroupProtoWithInstances("0invalid_group_name", home, HostArtifactsPath(),
                              HostArtifactsPath(), {{2, "name"}});
  auto result_non_qualifying_host_tool_dir = db.AddInstanceGroup(group_proto2);

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

  auto group_proto1 = GroupProtoWithInstances(
      "meow", home0, HostArtifactsPath(), HostArtifactsPath(), {{1, "name"}});
  ASSERT_TRUE(db.AddInstanceGroup(group_proto1).ok());

  auto group_proto2 = GroupProtoWithInstances(
      "miaou", home1, HostArtifactsPath(), HostArtifactsPath(), {{2, "name"}});
  ASSERT_TRUE(db.AddInstanceGroup(group_proto2).ok());
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

  auto group_proto1 = GroupProtoWithInstances(
      "meow", home, HostArtifactsPath(), HostArtifactsPath(), {{1, "name"}});
  ASSERT_TRUE(db.AddInstanceGroup(group_proto1).ok());
  auto group_proto2 = GroupProtoWithInstances(
      "meow", home, HostArtifactsPath(), HostArtifactsPath(), {{2, "name"}});
  ASSERT_FALSE(db.AddInstanceGroup(group_proto2).ok());
}

TEST_F(CvdInstanceDatabaseTest, Clear) {
  /* AddGroups(name):
   *   HOME: Workspace() + "/" + name
   *   HostArtifactsPath: Workspace() + "/" + "android_host_out"
   *   group_ := LocalInstanceGroup(name, HOME, HostArtifactsPath)
   */
  if (!SetUpOk() || !AddGroup("nyah", {InstanceProto(1, "name")}) ||
      !AddGroup("yah_ong", {InstanceProto(2, "name")})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();

  // test Clear()
  ASSERT_THAT(db.IsEmpty(), IsOkAndValue(false));
  ASSERT_TRUE(db.Clear().ok());
  ASSERT_THAT(db.IsEmpty(), IsOkAndValue(true));
}

TEST_F(CvdInstanceDatabaseTest, SearchGroups) {
  if (!SetUpOk() || !AddGroup("myau", {InstanceProto(1, "name")}) ||
      !AddGroup("miau", {InstanceProto(2, "name")})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  const std::string valid_home_search_key{Workspace() + "/" + "myau"};
  const std::string invalid_home_search_key{"/no/such/path"};

  auto valid_groups = db.FindGroups(Query{kHomeField, valid_home_search_key});
  auto valid_group = db.FindGroup({kHomeField, valid_home_search_key});
  auto invalid_groups = db.FindGroups(Query{kHomeField, invalid_home_search_key});
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
  if (!AddGroup("miaaaw", {InstanceProto(1, "name")}) ||
      !AddGroup("meow", {InstanceProto(2, "name")}) ||
      !AddGroup("mjau", {InstanceProto(3, "name")})) {
    GTEST_SKIP() << Error().msg;
  }
  auto eng_group = db.FindGroup({kHomeField, Workspace() + "/" + "meow"});
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
  ASSERT_TRUE(AddGroup({"yah_ong1"},
                       {InstanceProto(1, "yumi"), InstanceProto(2, "tiger")}));
  ASSERT_FALSE(AddGroup({"yah_ong2"},
                        {InstanceProto(3, "yumi"), InstanceProto(4, "yumi")}));
  ASSERT_FALSE(AddGroup({"yah_ong3"},
                        {InstanceProto(5, "yumi"), InstanceProto(5, "tiger")}));
  ASSERT_FALSE(AddGroup({"yah_ong4"},
                        {InstanceProto(1, "yumi"), InstanceProto(6, "tiger")}));
  auto kitty_group = db.FindGroup({kHomeField, Workspace() + "/" + "yah_ong1"});
  if (!kitty_group.ok()) {
    GTEST_SKIP() << "yah_ong1"
                 << " group was not found";
  }
  const auto& instances = kitty_group->Instances();
  for (auto const& instance : instances) {
    ASSERT_TRUE(instance.name() == "yumi" ||
                instance.name() == "tiger");
  }
}

TEST_F(CvdInstanceDatabaseTest, AddInstancesInvalid) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  ASSERT_FALSE(AddGroup("yah_ong", {InstanceProto(1, "!yumi")}));
  ASSERT_FALSE(AddGroup("yah_ong2", {InstanceProto(2, "ti ger")}));
}

TEST_F(CvdInstanceDatabaseTest, FindByInstanceId) {
  // The start of set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("miau",
                {InstanceProto(1, "8"), InstanceProto(10, "tv-instance")})) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("nyah",
                {InstanceProto(7, "my_favorite_phone"),
                 InstanceProto(11, "tv-instance"), InstanceProto(3, "3_")})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  auto miau_group = db.FindGroup({kHomeField, Workspace() + "/" + "miau"});
  auto nyah_group = db.FindGroup({kHomeField, Workspace() + "/" + "nyah"});
  if (!miau_group.ok() || !nyah_group.ok()) {
    GTEST_SKIP() << "miau or nyah group"
                 << " group was not found";
  }
  // The end of set up

  auto result1 = db.FindInstance(Query(kInstanceIdField, std::to_string(1)));
  auto result10 = db.FindInstance(Query(kInstanceIdField, std::to_string(10)));
  auto result7 = db.FindInstance(Query(kInstanceIdField, std::to_string(7)));
  auto result11 = db.FindInstance(Query(kInstanceIdField, std::to_string(11)));
  auto result3 = db.FindInstance(Query(kInstanceIdField, std::to_string(3)));
  auto result_invalid = db.FindInstance(Query(kInstanceIdField, std::to_string(20)));

  ASSERT_TRUE(result1.ok());
  ASSERT_TRUE(result10.ok());
  ASSERT_TRUE(result7.ok());
  ASSERT_TRUE(result11.ok());
  ASSERT_TRUE(result3.ok());
  ASSERT_EQ(result1->name(), "8");
  ASSERT_EQ(result10->name(), "tv-instance");
  ASSERT_EQ(result7->name(), "my_favorite_phone");
  ASSERT_EQ(result11->name(), "tv-instance");
  ASSERT_EQ(result3->name(), "3_");
  ASSERT_FALSE(result_invalid.ok());
}

TEST_F(CvdInstanceDatabaseTest, FindByPerInstanceName) {
  // starting set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("miau",
                {InstanceProto(1, "8"), InstanceProto(10, "tv_instance")})) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("nyah", {InstanceProto(7, "my_favorite_phone"),
                         InstanceProto(11, "tv_instance")})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  auto miau_group = db.FindGroup({kHomeField, Workspace() + "/" + "miau"});
  auto nyah_group = db.FindGroup({kHomeField, Workspace() + "/" + "nyah"});
  if (!miau_group.ok() || !nyah_group.ok()) {
    GTEST_SKIP() << "miau or nyah "
                 << " group was not found";
  }
  // end of set up

  auto result1 = db.FindInstance(Query(kInstanceNameField, "8"));
  auto result10_and_11 = db.FindInstances(Query(kInstanceNameField, "tv_instance"));
  auto result7 = db.FindInstance(Query(kInstanceNameField, "my_favorite_phone"));
  auto result_invalid =
      db.FindInstance(Query(kInstanceNameField, "name_never_seen"));

  ASSERT_TRUE(result1.ok());
  ASSERT_TRUE(result10_and_11.ok());
  ASSERT_TRUE(result7.ok());
  ASSERT_EQ(result10_and_11->size(), 2);
  ASSERT_EQ(result1->id(), 1);
  ASSERT_EQ(result7->id(), 7);
  ASSERT_FALSE(result_invalid.ok());
}

TEST_F(CvdInstanceDatabaseTest, FindInstancesByGroupName) {
  // starting set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("miau", {InstanceProto(1, "one")})) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("nyah", {InstanceProto(7, "my_favorite_phone"),
                         InstanceProto(11, "tv_instance")})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  auto nyah_group = db.FindGroup({kHomeField, Workspace() + "/" + "nyah"});
  if (!nyah_group.ok()) {
    GTEST_SKIP() << "nyah group was not found";
  }
  // end of set up

  auto result_nyah = db.FindInstances(Query(kGroupNameField, "nyah"));
  auto result_invalid = db.FindInstance(Query(kGroupNameField, "name_never_seen"));

  ASSERT_TRUE(result_nyah.ok());
  std::set<std::string> nyah_instance_names;
  for (const auto& instance : *result_nyah) {
    nyah_instance_names.insert(instance.name());
  }
  std::set<std::string> expected{"my_favorite_phone", "tv_instance"};
  ASSERT_EQ(nyah_instance_names, expected);
  ASSERT_FALSE(result_invalid.ok());
}

TEST_F(CvdInstanceDatabaseTest, FindGroupByPerInstanceName) {
  // starting set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("miau",
                {InstanceProto(1, "8"), InstanceProto(10, "tv_instance")})) {
    GTEST_SKIP() << Error().msg;
  }
  if (!AddGroup("nyah", {InstanceProto(7, "my_favorite_phone"),
                         InstanceProto(11, "tv_instance")})) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();
  auto miau_group = db.FindGroup({kHomeField, Workspace() + "/" + "miau"});
  auto nyah_group = db.FindGroup({kHomeField, Workspace() + "/" + "nyah"});
  if (!miau_group.ok() || !nyah_group.ok()) {
    GTEST_SKIP() << "miau or nyah "
                 << " group was not found";
  }
  // end of set up

  auto result_miau = db.FindGroups(Query(kInstanceNameField, "8"));
  auto result_both = db.FindGroups(Query(kInstanceNameField, "tv_instance"));
  auto result_nyah = db.FindGroups(Query(kInstanceNameField, "my_favorite_phone"));
  auto result_invalid = db.FindGroups(Query(kInstanceNameField, "name_never_seen"));

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

  ASSERT_TRUE(AddGroup("miau", {InstanceProto(1, "8"), InstanceProto(10, "tv_instance")}));

  auto result_8 = db.FindInstance(Query(kInstanceNameField, "8"));
  auto result_tv = db.FindInstance(Query(kInstanceNameField, "tv_instance"));

  ASSERT_TRUE(result_8.ok()) << result_8.error().Trace();
  ASSERT_TRUE(result_tv.ok()) << result_tv.error().Trace();
}

TEST_F(CvdInstanceDatabaseJsonTest, DumpLoadDumpCompare) {
  // starting set up
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  /*
   * Dumping to json, clearing up the DB, loading from the json,
   *
   */
  auto serialized_db =
      "{"
      "  \"Groups\" : ["
      "    {"
      "    \"Group Name\" : \"miau\","
      "    \"Host Tools Dir\" : \"/host/out/path\","
      "    \"Instances\" : ["
      "      {"
      "        \"Instance Id\" : \"1\","
      "        \"Parent Group\" : \"miau\","
      "        \"Per-Instance Name\" : \"8\""
      "      },{"
      "        \"Instance Id\" : \"10\","
      "        \"Parent Group\" : \"miau\","
      "        \"Per-Instance Name\" : \"tv_instance\""
      "      }"
      "    ],"
      "    \"Product Out Dir\" : \"/product/out/path\","
      "    \"Runtime/Home Dir\" : \"/home/dir\","
      "    \"Start Time\" : \"123456789\""
      "    }"
      "  ]"
      "}";
  auto json_parsing = ParseJson(serialized_db);
  ASSERT_TRUE(json_parsing.ok()) << serialized_db << std::endl
                                 << " is not a valid json.";
  auto& db = GetDb();
  auto load_result = db.LoadFromJson(*json_parsing);
  ASSERT_TRUE(load_result.ok()) << load_result.error().Trace();
  {
    // re-look up the group and the instances
    auto miau_group = db.FindGroup({kHomeField, std::string("/home/dir")});
    ASSERT_TRUE(miau_group.ok()) << miau_group.error().Trace();
    auto result_8 = db.FindInstance(Query(kInstanceNameField, "8"));
    auto result_tv = db.FindInstance(Query(kInstanceNameField, "tv_instance"));

    ASSERT_TRUE(result_8.ok()) << result_8.error().Trace();
    ASSERT_TRUE(result_tv.ok()) << result_tv.error().Trace();
  }
}

TEST_F(CvdInstanceDatabaseTest, UpdateInstances) {
  if (!SetUpOk()) {
    GTEST_SKIP() << Error().msg;
  }
  auto& db = GetDb();

  cvd::InstanceGroup grp;
  grp.set_home_directory(Workspace() + "/grp1_home");
  grp.set_name("grp1");
  auto ins1 = grp.add_instances();
  ins1->set_name("ins1");
  ins1->set_state(cvd::INSTANCE_STATE_PREPARING);
  auto ins2 = grp.add_instances();
  ins2->set_name("ins2");
  ins2->set_state(cvd::INSTANCE_STATE_PREPARING);

  auto add_res = db.AddInstanceGroup(grp);
  ASSERT_TRUE(add_res.ok())
      << "Failed to add group to db: " << add_res.error().Message();

  auto instance_group = *(std::move(add_res));
  ASSERT_TRUE(instance_group.ProductOutPath().empty());
  instance_group.SetProductOutPath("/path/to/product");
  auto& instance1 = instance_group.Instances()[0];
  instance1.set_id(1);
  instance1.set_state(cvd::INSTANCE_STATE_STARTING);
  auto& instance2 = instance_group.Instances()[1];
  instance2.set_id(2);
  instance2.set_state(cvd::INSTANCE_STATE_STARTING);

  auto update_res = db.UpdateInstanceGroup(instance_group);
  ASSERT_TRUE(update_res.ok())
      << "Failed to update database: " << update_res.error().Message();

  auto find_res = db.FindGroup(Query("group_name", "grp1"));
  ASSERT_TRUE(find_res.ok()) << find_res.error().Message();

  EXPECT_EQ(find_res->HomeDir(), Workspace() + "/grp1_home");
  EXPECT_EQ(find_res->ProductOutPath(), "/path/to/product");
  EXPECT_EQ(find_res->Instances()[0].id(), 1);
  EXPECT_EQ(find_res->Instances()[1].id(), 2);
  EXPECT_EQ(find_res->Instances()[0].state(), cvd::INSTANCE_STATE_STARTING);
  EXPECT_EQ(find_res->Instances()[1].state(), cvd::INSTANCE_STATE_STARTING);
}

}  // namespace selector
}  // namespace cuttlefish
