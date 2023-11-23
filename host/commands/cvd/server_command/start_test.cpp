/*
 * Copyright (C) 2023 The Android Open Source Project
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
//

#include "host/commands/cvd/server_command/start.h"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "common/libs/utils/result_matchers.h"

namespace cuttlefish {

TEST(CvdStart, DaemonFlag) {
  ASSERT_THAT(IsDaemonModeFlag({}), IsOkAndValue(false));
  ASSERT_THAT(IsDaemonModeFlag({"--daemon"}), IsOkAndValue(true));
  ASSERT_THAT(IsDaemonModeFlag({"--nodaemon"}), IsOkAndValue(false));
  ASSERT_THAT(IsDaemonModeFlag({"--daemon=y"}), IsOkAndValue(true));
  ASSERT_THAT(IsDaemonModeFlag({"--daemon=TRUE"}), IsOkAndValue(true));
  ASSERT_THAT(IsDaemonModeFlag({"--daemon=true"}), IsOkAndValue(true));
  ASSERT_THAT(IsDaemonModeFlag({"--daemon=false"}), IsOkAndValue(false));
  ASSERT_THAT(IsDaemonModeFlag({"--daemon=FALSE"}), IsOkAndValue(false));
  ASSERT_THAT(IsDaemonModeFlag({"--daemon=no"}), IsOkAndValue(false));

  ASSERT_THAT(IsDaemonModeFlag({"--daemon=true,false"}), IsError());
  ASSERT_THAT(IsDaemonModeFlag({"--daemon=hello"}), IsOkAndValue(true));
}

}  // namespace cuttlefish
