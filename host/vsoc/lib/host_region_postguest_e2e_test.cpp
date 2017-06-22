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
 * This test must be run after the initial guest-side tests. It verifies that
 * the control strings written by the guest are visible from the host.
 */
#include "host/vsoc/lib/host_region.h"

#include "common/vsoc/shm/e2e_test_region.h"
#include <gtest/gtest.h>

using vsoc::layout::e2e_test::E2EPrimaryTestRegion;
using vsoc::layout::e2e_test::E2ESecondaryTestRegion;

/**
 * The string functions have problems with volatile pointers, so
 * this function casts them away.
 */
template <typename T>
T* make_nonvolatile(volatile T* in) {
  return (T*)in;
}

template <typename Layout>
class PostGuestRegionTest {
 public:
  vsoc::TypedRegion<Layout> region;

  void CheckGuestStrings() {
    ASSERT_TRUE(region.Open());
    size_t num_data = Layout::NumFillRecords(region.region_data_size());
    EXPECT_LE(2, num_data);
    Layout* r = region.data();
    for (size_t i = 0; i < num_data; ++i) {
      EXPECT_STREQ(Layout::guest_pattern,
                   make_nonvolatile(r->data[i].guest_writable));
    }
  }
};

TEST(PostGuestRegionTest, PrimaryRegionGuestWritesVisible) {
  PostGuestRegionTest<E2EPrimaryTestRegion> test;
  test.CheckGuestStrings();
}

TEST(PostGuestRegionTest, SecondaryRegionGuestWritesVisible) {
  PostGuestRegionTest<E2ESecondaryTestRegion> test;
  test.CheckGuestStrings();
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  vsoc::TypedRegion<E2EPrimaryTestRegion> region;
  region.Open();
  E2EPrimaryTestRegion* r = region.data();
  // Wait until the guest has filled its memory before proceeding with this
  // test.
  // TODO(ghartman): Upgrade this to a futex when we have support.
  while (r->guest_status.value() < vsoc::layout::e2e_test::E2E_MEMORY_FILLED) {
    sleep(1);
  }
  int rval = RUN_ALL_TESTS();
  if (!rval) {
    r->host_status.set_value(vsoc::layout::e2e_test::E2E_PEER_MEMORY_READ);
  }
  return rval;
}
