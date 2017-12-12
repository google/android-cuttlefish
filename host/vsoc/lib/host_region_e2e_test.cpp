/*
 * Copyright (C) 2016 The Android Open Source Project
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
 * End-to-end test to ensure that mapping of vsoc regions works on the host.
 */

#include <gtest/gtest.h>
#include "common/vsoc/lib/e2e_test_region_view.h"

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
void SetHostStrings(View* in) {
  size_t num_data = in->string_size();
  EXPECT_LE(2, num_data);
  for (size_t i = 0; i < num_data; ++i) {
    EXPECT_TRUE(!in->host_string(i)[0] ||
                !strcmp(in->host_string(i), View::Layout::host_pattern));
    in->set_host_string(i, View::Layout::host_pattern);
    EXPECT_STREQ(in->host_string(i), View::Layout::host_pattern);
  }
}

template <typename View>
void CheckPeerStrings(View* in) {
  size_t num_data = in->string_size();
  EXPECT_LE(2, num_data);
  for (size_t i = 0; i < num_data; ++i) {
    EXPECT_STREQ(View::Layout::guest_pattern, in->guest_string(i));
  }
}

TEST(RegionTest, PeerTests) {
  vsoc::E2EPrimaryRegionView primary;
  ASSERT_TRUE(primary.Open());
  vsoc::E2ESecondaryRegionView secondary;
  ASSERT_TRUE(secondary.Open());
  LOG(INFO) << "Regions are open";
  SetHostStrings(&primary);
  EXPECT_FALSE(secondary.HasIncomingInterrupt());
  EXPECT_TRUE(primary.MaybeInterruptPeer());
  LOG(INFO) << "Waiting for first interrupt from peer";
  primary.WaitForInterrupt();
  LOG(INFO) << "First interrupt received";
  CheckPeerStrings(&primary);
  SetHostStrings(&secondary);
  EXPECT_TRUE(secondary.MaybeInterruptPeer());
  LOG(INFO) << "Waiting for second interrupt from peer";
  secondary.WaitForInterrupt();
  LOG(INFO) << "Second interrupt received";
  CheckPeerStrings(&secondary);

  // Test signals
  EXPECT_FALSE(secondary.HasIncomingInterrupt());
  LOG(INFO) << "Verified no early second signal";
  vsoc::layout::Sides side;
  side.value_ = vsoc::layout::Sides::Peer;
  primary.SendSignal(side, &primary.data()->host_to_guest_signal);
  LOG(INFO) << "Signal sent. Waiting for first signal from peer";
  primary.WaitForInterrupt();
  int count = 0; // counts the number of signals received.
  primary.ProcessSignalsFromPeer([&primary, &count](uint32_t* uaddr){
      ++count;
      EXPECT_TRUE(uaddr == &primary.data()->guest_to_host_signal);
    });
  EXPECT_TRUE(count == 1);
  LOG(INFO) << "Signal received on primary region";
  secondary.SendSignal(side, &secondary.data()->host_to_guest_signal);
  LOG(INFO) << "Signal sent. Waiting for second signal from peer";
  secondary.WaitForInterrupt();
  count = 0;
  secondary.ProcessSignalsFromPeer([&secondary, &count](uint32_t* uaddr){
      ++count;
      EXPECT_TRUE(uaddr == &secondary.data()->guest_to_host_signal);
    });
  EXPECT_TRUE(count == 1);
  LOG(INFO) << "Signal received on secondary region";

  EXPECT_FALSE(primary.HasIncomingInterrupt());
  EXPECT_FALSE(secondary.HasIncomingInterrupt());
}

TEST(RegionTest, MissingRegionCausesDeath) {
  vsoc::E2EUnfindableRegionView test;
  EXPECT_DEATH(test.Open(), ".*");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  int rval = RUN_ALL_TESTS();
  if (!rval) {
    vsoc::E2EPrimaryRegionView region;
    region.Open();
    region.host_status(vsoc::layout::e2e_test::E2E_MEMORY_FILLED);
  }
  return rval;
}
