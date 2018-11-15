/*
 * Copyright (C) 2017 The Android Open Source Project
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
 * End-to-end test to ensure that mapping of vsoc regions works on the guest.
 */

#include "guest/vsoc/lib/e2e_test_common.h"
#include "common/vsoc/lib/e2e_test_region_view.h"

#include <android-base/logging.h>
#include <gtest/gtest.h>

template <typename View>
void DeathTestView() {
  disable_tombstones();
  // View::GetInstance should never return.
  EXPECT_FALSE(!!View::GetInstance());
}

// Here is a summary of the two regions interrupt and write test:
// 1. Write our strings to the first region
// 2. Ensure that our peer hasn't signalled the second region. That would
//    indicate that it didn't wait for our interrupt.
// 3. Send the interrupt on the first region
// 4. Wait for our peer's interrupt on the first region
// 5. Confirm that we can see our peer's writes in the first region
// 6. Initialize our strings in the second region
// 7. Send an interrupt on the second region to our peer
// 8. Wait for our peer's interrupt on the second region
// 9. Confirm that we can see our peer's writes in the second region
// 10. Repeat the process for signaling.
// 11. Confirm that no interrupt is pending in the first region
// 12. Confirm that no interrupt is pending in the second region

template <typename View>
void SetGuestStrings(View* in) {
  size_t num_data = in->string_size();
  EXPECT_LE(2U, num_data);
  for (size_t i = 0; i < num_data; ++i) {
    EXPECT_TRUE(!in->guest_string(i)[0] ||
                !strcmp(in->guest_string(i), View::Layout::guest_pattern));
    in->set_guest_string(i, View::Layout::guest_pattern);
    EXPECT_STREQ(in->guest_string(i), View::Layout::guest_pattern);
  }
}

template <typename View>
void CheckPeerStrings(View* in) {
  size_t num_data = in->string_size();
  EXPECT_LE(2U, num_data);
  for (size_t i = 0; i < num_data; ++i) {
    EXPECT_STREQ(View::Layout::host_pattern, in->host_string(i));
  }
}

TEST(RegionTest, BasicPeerTests) {
  auto primary = vsoc::E2EPrimaryRegionView::GetInstance();
  auto secondary = vsoc::E2ESecondaryRegionView::GetInstance();
  ASSERT_TRUE(!!primary);
  ASSERT_TRUE(!!secondary);
  LOG(INFO) << "Regions are open";
  SetGuestStrings(primary);
  LOG(INFO) << "Primary guest strings are set";
  EXPECT_FALSE(secondary->HasIncomingInterrupt());
  LOG(INFO) << "Verified no early second interrupt";
  EXPECT_TRUE(primary->MaybeInterruptPeer());
  LOG(INFO) << "Interrupt sent. Waiting for first interrupt from peer";
  primary->WaitForInterrupt();
  LOG(INFO) << "First interrupt received";
  CheckPeerStrings(primary);
  LOG(INFO) << "Verified peer's primary strings";
  SetGuestStrings(secondary);
  LOG(INFO) << "Secondary guest strings are set";
  EXPECT_TRUE(secondary->MaybeInterruptPeer());
  LOG(INFO) << "Second interrupt sent";
  secondary->WaitForInterrupt();
  LOG(INFO) << "Second interrupt received";
  CheckPeerStrings(secondary);
  LOG(INFO) << "Verified peer's secondary strings";

  // Test signals
  EXPECT_FALSE(secondary->HasIncomingInterrupt());
  LOG(INFO) << "Verified no early second signal";
  primary->SendSignal(vsoc::layout::Sides::Peer,
                      &primary->data()->guest_to_host_signal);
  LOG(INFO) << "Signal sent. Waiting for first signal from peer";
  primary->WaitForInterrupt();
  int count = 0;  // counts the number of signals received.
  primary->ProcessSignalsFromPeer(
      [&primary, &count](uint32_t offset) {
        ++count;
        EXPECT_TRUE(offset == primary->host_to_guest_signal_offset());
      });
  EXPECT_TRUE(count == 1);
  LOG(INFO) << "Signal received on primary region";
  secondary->SendSignal(vsoc::layout::Sides::Peer,
                        &secondary->data()->guest_to_host_signal);
  LOG(INFO) << "Signal sent. Waiting for second signal from peer";
  secondary->WaitForInterrupt();
  count = 0;
  secondary->ProcessSignalsFromPeer(
      [&secondary, &count](uint32_t offset) {
        ++count;
        EXPECT_TRUE(offset == secondary->host_to_guest_signal_offset());
      });
  EXPECT_TRUE(count == 1);
  LOG(INFO) << "Signal received on secondary region";

  EXPECT_FALSE(primary->HasIncomingInterrupt());
  EXPECT_FALSE(secondary->HasIncomingInterrupt());
  LOG(INFO) << "PASS: BasicPeerTests";
}

TEST(RegionTest, MissingRegionDeathTest) {
  // EXPECT_DEATH creates a child for the test, so we do it out here.
  // DeathTestGuestRegion will actually do the deadly call after ensuring
  // that we don't create an unwanted tombstone.
  EXPECT_EXIT(DeathTestView<vsoc::E2EUnfindableRegionView>(),
              testing::ExitedWithCode(2),
              ".*" DEATH_TEST_MESSAGE ".*");
}

int main(int argc, char** argv) {
  android::base::InitLogging(argv);
  testing::InitGoogleTest(&argc, argv);
  int rval = RUN_ALL_TESTS();
  if (!rval) {
    auto region = vsoc::E2EPrimaryRegionView::GetInstance();
    region->guest_status(vsoc::layout::e2e_test::E2E_MEMORY_FILLED);
    LOG(INFO) << "stage_1_guest_region_e2e_tests PASSED";
  } else {
    LOG(ERROR) << "stage_1_guest_region_e2e_tests FAILED";
  }
  return rval;
}
