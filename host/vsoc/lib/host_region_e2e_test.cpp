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

#include "host/vsoc/lib/host_region.h"

#include "common/vsoc/shm/e2e_test_region.h"

#include <gtest/gtest.h>

using vsoc::layout::e2e_test::E2EPrimaryTestRegionLayout;
using vsoc::layout::e2e_test::E2ESecondaryTestRegionLayout;

/**
 * The string functions have problems with volatile pointers, so
 * this function casts them away.
 */
template <typename T>
T* make_nonvolatile(volatile T* in) {
  return (T*)in;
}

template <typename Layout>
class RegionTest {
 public:
  vsoc::TypedRegionView<Layout> region;

  void CheckPeerStrings() {
    size_t num_data = Layout::NumFillRecords(region.region_data_size());
    EXPECT_LE(2, num_data);
    Layout* r = region.data();
    for (size_t i = 0; i < num_data; ++i) {
      EXPECT_STREQ(Layout::guest_pattern,
                   make_nonvolatile(r->data[i].guest_writable));
    }
  }

  bool HasIncomingInterruptFromPeer() {
    return region.HasIncomingInterrupt();
  }

  void SendInterruptToPeer() {
    region.InterruptPeer();
  }

  void WaitForInterruptFromPeer() {
    region.WaitForInterrupt();
  }

  void WriteStrings() {
    size_t num_data = Layout::NumFillRecords(region.region_data_size());
    EXPECT_LE(2, num_data);
    Layout* r = region.data();
    for (size_t i = 0; i < num_data; ++i) {
      EXPECT_TRUE(!r->data[i].host_writable[0] ||
                  !strcmp(make_nonvolatile(r->data[i].host_writable),
                          Layout::host_pattern));
      strcpy(make_nonvolatile(r->data[i].host_writable), Layout::host_pattern);
      EXPECT_STREQ(Layout::host_pattern,
                   make_nonvolatile(r->data[i].host_writable));
    }
  }
};

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
// 10. Confirm that no interrupt is pending in the first region
// 11. Confirm that no interrupt is pending in the second region

TEST(RegionTest, PeerTests) {
  RegionTest<E2EPrimaryTestRegionLayout> primary;
  RegionTest<E2ESecondaryTestRegionLayout> secondary;
  ASSERT_TRUE(primary.region.Open());
  ASSERT_TRUE(secondary.region.Open());
  LOG(INFO) << "Regions are open";
  primary.WriteStrings();
  EXPECT_FALSE(secondary.HasIncomingInterruptFromPeer());
  primary.SendInterruptToPeer();
  LOG(INFO) << "Waiting for first interrupt from peer";
  primary.WaitForInterruptFromPeer();
  LOG(INFO) << "First interrupt received";
  primary.CheckPeerStrings();
  secondary.WriteStrings();
  secondary.SendInterruptToPeer();
  LOG(INFO) << "Waiting for second interrupt from peer";
  secondary.WaitForInterruptFromPeer();
  LOG(INFO) << "Second interrupt received";
  secondary.CheckPeerStrings();
  EXPECT_FALSE(primary.HasIncomingInterruptFromPeer());
  EXPECT_FALSE(secondary.HasIncomingInterruptFromPeer());
}

/**
 * Defines an end-to-end region with a name that should never be configured.
 */
struct UnfindableRegionLayout : public E2EPrimaryTestRegionLayout {
  static const char* region_name;
};

const char* UnfindableRegionLayout::region_name = "e2e_must_not_exist";

TEST(RegionTest, MissingRegionCausesDeath) {
  RegionTest<UnfindableRegionLayout> test;
  EXPECT_DEATH(test.region.Open(), ".*");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  int rval = RUN_ALL_TESTS();
  if (!rval) {
    vsoc::TypedRegionView<E2EPrimaryTestRegionLayout> region;
    region.Open();
    E2EPrimaryTestRegionLayout* r = region.data();
    r->host_status.set_value(vsoc::layout::e2e_test::E2E_MEMORY_FILLED);
  }
  return rval;
}
