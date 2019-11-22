/*
 * Copyright (C) 2018 The Android Open Source Project
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

/*
 * Stand-alone tests for the ioctls in the vsoc driver.
 */

#include "uapi/vsoc_shm.h"
#include <atomic>
#include <stdint.h>
#include "common/vsoc/lib/e2e_test_region_view.h"
#include "guest/vsoc/lib/manager_region_view.h"

#include <android-base/logging.h>
#include <gtest/gtest.h>

void BasicWaitForSignal(vsoc::E2EPrimaryRegionView* region,
                        uint32_t expected_start,
                        uint32_t expected_finish) {
  ASSERT_EQ(expected_start, region->read_guest_self_register());
  int rval = region->wait_guest_self_register(expected_start);
  EXPECT_LE(0, rval);
  EXPECT_GT(5, rval);
  EXPECT_EQ(expected_finish, region->read_guest_self_register());
}

TEST(FutexTest, BasicFutexTests) {
  constexpr uint32_t INITIAL_SIGNAL = 0;
  constexpr uint32_t SILENT_UPDATE_SIGNAL = 1;
  constexpr uint32_t WAKE_SIGNAL = 2;
  auto region = vsoc::E2EPrimaryRegionView::GetInstance();
  ASSERT_TRUE(region != NULL);
  LOG(INFO) << "Regions are open";
  region->write_guest_self_register(INITIAL_SIGNAL);
  std::thread waiter(BasicWaitForSignal, region, INITIAL_SIGNAL, WAKE_SIGNAL);
  sleep(1);
  LOG(INFO) << "Still waiting. Trying to wake wrong address";
  region->signal_guest_to_host_register();
  sleep(1);
  LOG(INFO) << "Still waiting. Trying to wake without update";
  region->signal_guest_self_register();
  sleep(1);
  LOG(INFO) << "Still waiting. Trying to wake without signal";
  region->write_guest_self_register(SILENT_UPDATE_SIGNAL);
  sleep(1);
  LOG(INFO) << "Still waiting. Trying to wake with signal";
  region->write_guest_self_register(WAKE_SIGNAL);
  region->signal_guest_self_register();
  waiter.join();
  LOG(INFO) << "Wake worked";
  LOG(INFO) << "PASS: BasicPeerTests";
}

int main(int argc, char* argv[]) {
  android::base::InitLogging(argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
