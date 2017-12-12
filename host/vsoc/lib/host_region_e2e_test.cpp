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
class RegionTest {
 public:
  vsoc::TypedRegion<Layout> region;

  void TestHostRegion() {
    EXPECT_TRUE(region.Open());
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

  void SendInterruptToGuest() {
    EXPECT_TRUE(region.Open());
    region.InterruptPeer();
  }
};

TEST(RegionTest, PrimaryRegionWritable) {
  RegionTest<E2EPrimaryTestRegion> test;
  test.TestHostRegion();
}

TEST(RegionTest, PrimaryRegionInterrupt) {
  RegionTest<E2EPrimaryTestRegion> test;
  test.SendInterruptToGuest();
}

TEST(RegionTest, SecondaryRegionWritable) {
  RegionTest<E2ESecondaryTestRegion> test;
  test.TestHostRegion();
}

TEST(RegionTest, SecondarRegionInterrupt) {
  RegionTest<E2ESecondaryTestRegion> test;
  test.SendInterruptToGuest();
}

/**
 * Defines an end-to-end region with a name that should never be configured.
 */
struct UnfindableRegion : public E2EPrimaryTestRegion {
  static const char* region_name;
};

const char* UnfindableRegion::region_name = "e2e_must_not_exist";

TEST(RegionTest, MissingRegionCausesDeath) {
  RegionTest<UnfindableRegion> test;
  EXPECT_DEATH(test.TestHostRegion(), ".*");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  int rval = RUN_ALL_TESTS();
  if (!rval) {
    vsoc::TypedRegion<E2EPrimaryTestRegion> region;
    region.Open();
    E2EPrimaryTestRegion* r = region.data();
    r->host_status.set_value(vsoc::layout::e2e_test::E2E_MEMORY_FILLED);
  }
  return rval;
}
