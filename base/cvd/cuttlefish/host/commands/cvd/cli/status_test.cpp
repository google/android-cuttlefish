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

#include "cuttlefish/host/commands/cvd/cli/commands/status.h"

#include <sstream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "json/value.h"

#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/commands/cvd/cli/fake_instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(CvdStatusTest, ReturnsStoppedWhenGroupStopsMidFetch) {
  FakeInstanceManager manager;
  manager.SetGroups(
      {CF_ASSERT(MakeGroup("grp1", 1, cvd::INSTANCE_STATE_RUNNING))});
  manager.OnPrune([&manager](int call) {
    if (call == 2) {
      manager.SetGroups(
          {CF_ASSERT(MakeGroup("grp1", 1, cvd::INSTANCE_STATE_STOPPED))});
    }
  });

  std::ostringstream out;
  CvdStatusCommandHandler handler(manager, out);
  ASSERT_THAT(handler.Handle(CF_ASSERT(MakeRequest("status", {"--print"}))),
              IsOk());

  Json::Value parsed = CF_ASSERT(ParseJson(out.str()));
  EXPECT_EQ(parsed["instances"][0]["status"].asString(), "Stopped");
}

TEST(CvdStatusTest, PropagatesMissingStatusToolErrorOnStillActiveGroup) {
  FakeInstanceManager manager;
  manager.SetGroups(
      {CF_ASSERT(MakeGroup("grp1", 1, cvd::INSTANCE_STATE_RUNNING))});

  std::ostringstream out;
  CvdStatusCommandHandler handler(manager, out);
  EXPECT_THAT(handler.Handle(CF_ASSERT(MakeRequest("status", {"--print"}))),
              IsError());
  EXPECT_THAT(handler.Handle(CF_ASSERT(
                  MakeRequest("status", {"--print", "--instance_name=cvd-1"}))),
              IsError());
}

}  // namespace
}  // namespace cuttlefish
