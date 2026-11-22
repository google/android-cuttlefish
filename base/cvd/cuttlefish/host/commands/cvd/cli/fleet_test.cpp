/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/cvd/cli/commands/fleet.h"

#include <sstream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "json/value.h"

#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/commands/cvd/cli/fake_instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/host/commands/cvd/instances/local_instance_group.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(CvdFleetTest, PrunesAndSkipsGroupRemovedMidIteration) {
  FakeInstanceManager manager;
  manager.SetGroups(
      {CF_ASSERT(MakeGroup("alive", 1, cvd::INSTANCE_STATE_STOPPED)),
       CF_ASSERT(MakeGroup("vanishing", 2, cvd::INSTANCE_STATE_RUNNING))});
  manager.OnPrune([&manager](int call) {
    if (call == 2) {
      // Simulate concurrent removal of `vanishing` while `fleet` iterates.
      manager.SetGroups(
          {CF_ASSERT(MakeGroup("alive", 1, cvd::INSTANCE_STATE_STOPPED))});
    }
  });

  std::ostringstream out;
  CvdFleetCommandHandler handler(manager, out);
  ASSERT_THAT(handler.Handle(CF_ASSERT(MakeRequest("fleet"))), IsOk());

  EXPECT_EQ(manager.prune_calls(), 2);
  Json::Value parsed = CF_ASSERT(ParseJson(out.str()));
  ASSERT_EQ(parsed["groups"].size(), 1u);
  EXPECT_EQ(parsed["groups"][0]["group_name"].asString(), "alive");
}

TEST(CvdFleetTest, DegradesStillActiveMissingStatusToolToUnreachable) {
  FakeInstanceManager manager;
  manager.SetGroups(
      {CF_ASSERT(MakeGroup("active", 1, cvd::INSTANCE_STATE_RUNNING))});

  std::ostringstream out;
  CvdFleetCommandHandler handler(manager, out);
  ASSERT_THAT(handler.Handle(CF_ASSERT(MakeRequest("fleet"))), IsOk());

  Json::Value parsed = CF_ASSERT(ParseJson(out.str()));
  ASSERT_EQ(parsed["groups"].size(), 1u);
  EXPECT_EQ(parsed["groups"][0]["instances"][0]["status"].asString(),
            "Unreachable");
  EXPECT_GE(manager.persist_calls(), 1);
  LocalInstanceGroup stored =
      CF_ASSERT(manager.FindGroup({.group_name = "active"}));
  EXPECT_EQ(stored.Instances()[0].State(), cvd::INSTANCE_STATE_UNREACHABLE);
}

}  // namespace
}  // namespace cuttlefish
