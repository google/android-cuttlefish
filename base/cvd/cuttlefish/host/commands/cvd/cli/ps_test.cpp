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

#include "cuttlefish/host/commands/cvd/cli/commands/ps.h"

#include <sstream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/commands/cvd/cli/fake_instance_manager.h"
#include "cuttlefish/host/commands/cvd/instances/cvd_persistent_data.pb.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;

TEST(CvdPsTest, PrunesAndSkipsGroupRemovedMidIteration) {
  FakeInstanceManager manager;
  manager.SetGroups(
      {CF_ASSERT(MakeGroup("kept", 1, cvd::INSTANCE_STATE_STOPPED)),
       CF_ASSERT(MakeGroup("gone", 2, cvd::INSTANCE_STATE_RUNNING))});
  manager.OnPrune([&manager](int call) {
    if (call == 2) {
      manager.SetGroups(
          {CF_ASSERT(MakeGroup("kept", 1, cvd::INSTANCE_STATE_STOPPED))});
    }
  });

  std::ostringstream out;
  CvdPsCommandHandler handler(manager, out);
  ASSERT_THAT(handler.Handle(CF_ASSERT(MakeRequest("ps"))), IsOk());

  EXPECT_THAT(out.str(), HasSubstr("kept"));
  EXPECT_THAT(out.str(), Not(HasSubstr("gone")));
}

}  // namespace
}  // namespace cuttlefish
