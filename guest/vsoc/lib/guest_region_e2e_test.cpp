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
 * Integration test to ensure that the guest can map vsoc regions.
 */

#include "guest/vsoc/lib/guest_region.h"
#include "common/vsoc/shm/e2e_test_region.h"

#include <android-base/logging.h>
#include <gtest/gtest.h>

#define DEATH_TEST_MESSAGE "abort converted to exit of 2 during death test"

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

  void TestGuestRegion()  {
    ASSERT_TRUE(region.Open());
    size_t num_data = Layout::NumFillRecords(region.region_data_size());
    EXPECT_LE(2, num_data);
    Layout* r = region.data();
    for (size_t i = 0; i < num_data; ++i) {
      EXPECT_TRUE(!r->data[i].guest_writable[0] ||
                  !strcmp(make_nonvolatile(r->data[i].guest_writable),
                          Layout::guest_pattern));
      strcpy(make_nonvolatile(r->data[i].guest_writable),
             Layout::guest_pattern);
      EXPECT_STREQ(Layout::guest_pattern,
                   make_nonvolatile(r->data[i].guest_writable));
    }
  }

  void DeathTestGuestRegion()  {
    // We don't want a tombstone, and we're already in the child.
    // region, so we modify the behavior of LOG(ABORT) to print the
    // well known message and do an error-based exit.
    android::base::SetAborter([](const char*){
        fputs(DEATH_TEST_MESSAGE, stderr);
        fflush(stderr);
        exit(2);
      });
    // region.Open should never return.
    EXPECT_FALSE(region.Open());
  }
};

TEST(RegionTest, PrimaryRegionWritable) {
  RegionTest<E2EPrimaryTestRegion> test;
  test.TestGuestRegion();
}

TEST(RegionTest, SecondaryRegionWritable) {
  RegionTest<E2ESecondaryTestRegion> test;
  test.TestGuestRegion();
}

/**
 * Defines an end-to-end region with a name that should never be configured.
 */
struct UnfindableRegion : public E2EPrimaryTestRegion {
  static const char* region_name;
};

const char* UnfindableRegion::region_name = "e2e_must_not_exist";

TEST(RegionTest, MissingRegionDeathTest) {
  RegionTest<UnfindableRegion> region;
  // EXPECT_DEATH creates a child for the test, so we do it out here.
  // DeathTestGuestRegion will actually do the deadly call after ensuring
  // that we don't create an unwanted tombstone.
  EXPECT_EXIT(region.DeathTestGuestRegion(), testing::ExitedWithCode(2),
              ".*" DEATH_TEST_MESSAGE ".*");
}

int main(int argc, char** argv) {
  android::base::InitLogging(argv);
  testing::InitGoogleTest(&argc, argv);
  int rval = RUN_ALL_TESTS();
  if (!rval) {
    vsoc::TypedRegion<E2EPrimaryTestRegion> region;
    region.Open();
    E2EPrimaryTestRegion* r = region.data();
    r->guest_status.set_value(vsoc::layout::e2e_test::E2E_MEMORY_FILLED);
    LOG(INFO) << "stage_1_guest_region_e2e_tests PASSED";
  }
  return rval;
}
