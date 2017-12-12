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

using vsoc::layout::e2e_test::E2EPrimaryTestRegionLayout;
using vsoc::layout::e2e_test::E2ESecondaryTestRegionLayout;
using vsoc::layout::e2e_test::E2EManagedTestRegionLayout;
using vsoc::layout::e2e_test::E2EManagerTestRegionLayout;

static inline void disable_tombstones() {
  // We don't want a tombstone, and we're already in the child, so we modify the
  // behavior of LOG(ABORT) to print the well known message and do an
  // error-based exit.
  android::base::SetAborter([](const char*){
      fputs(DEATH_TEST_MESSAGE, stderr);
      fflush(stderr);
      exit(2);
    });
}

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
    ASSERT_TRUE(region.Open());
    size_t num_data = Layout::NumFillRecords(region.region_data_size());
    EXPECT_LE(2, num_data);
    Layout* r = region.data();
    for (size_t i = 0; i < num_data; ++i) {
      EXPECT_STREQ(Layout::host_pattern,
                   make_nonvolatile(r->data[i].host_writable));
    }
  }

  bool HasIncomingInterruptFromPeer() {
    return region.HasIncomingInterrupt();
  }

  void DeathTestGuestRegion()  {
    disable_tombstones();
    // region.Open should never return.
    EXPECT_FALSE(region.Open());
  }

  void SendInterruptToPeer() {
    EXPECT_TRUE(region.Open());
    region.InterruptPeer();
  }

  void WaitForInterruptFromPeer() {
    ASSERT_TRUE(region.Open());
    region.WaitForInterrupt();
  }

  void WriteStrings()  {
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
struct UnfindableRegionView : public E2EPrimaryTestRegionLayout {
  static const char* region_name;
};

const char* UnfindableRegionView::region_name = "e2e_must_not_exist";

TEST(RegionTest, MissingRegionDeathTest) {
  RegionTest<UnfindableRegionView> test;
  // EXPECT_DEATH creates a child for the test, so we do it out here.
  // DeathTestGuestRegion will actually do the deadly call after ensuring
  // that we don't create an unwanted tombstone.
  EXPECT_EXIT(test.DeathTestGuestRegion(), testing::ExitedWithCode(2),
              ".*" DEATH_TEST_MESSAGE ".*");
}

class ManagedRegionTest {
 public:
  void testManagedRegionFailMap() {
    vsoc::TypedRegionView<E2EManagedTestRegionLayout> managed_region;
    disable_tombstones();
    // managed_region.Open should never return.
    EXPECT_FALSE(managed_region.Open());
  }

  void testManagedRegionMap() {
    EXPECT_TRUE(manager_region_.Open());

    // Maps correctly with permission
    const uint32_t owned_value = 65,
        begin_offset = 4096,
        end_offset = 8192;
    int perm_fd = manager_region_.CreateFdScopedPermission(
        &manager_region_.data()->data[0],
        owned_value,
        begin_offset,
        end_offset);
    EXPECT_TRUE(perm_fd >= 0);
    fd_scoped_permission perm;
    ASSERT_TRUE(ioctl(perm_fd, VSOC_GET_FD_SCOPED_PERMISSION, &perm) == 0);
    void* mapped_ptr = mmap(NULL,
                            perm.end_offset - perm.begin_offset,
                            PROT_WRITE | PROT_READ,
                            MAP_SHARED,
                            perm_fd,
                            0);
    EXPECT_FALSE(mapped_ptr == MAP_FAILED);

    // Owned value gets written
    EXPECT_TRUE(manager_region_.data()->data[0] == owned_value);

    // Data written to the mapped memory stays there after unmap
    std::string str = "managed by e2e_manager";
    strcpy(reinterpret_cast<char*>(mapped_ptr), str.c_str());
    EXPECT_TRUE(munmap(mapped_ptr, end_offset - begin_offset) == 0);
    mapped_ptr = mmap(NULL,
                      end_offset - begin_offset,
                      PROT_WRITE | PROT_READ,
                      MAP_SHARED,
                      perm_fd,
                      0);
    EXPECT_FALSE(mapped_ptr == MAP_FAILED);
    EXPECT_TRUE(strcmp(reinterpret_cast<char*>(mapped_ptr), str.c_str()) == 0);

    // Create permission elsewhere in the region, map same offset and length,
    // ensure data isn't there
    EXPECT_TRUE(munmap(mapped_ptr, end_offset - begin_offset) == 0);
    close(perm_fd);
    EXPECT_TRUE(manager_region_.data()->data[0] == 0);
    perm_fd = manager_region_.CreateFdScopedPermission(
        &manager_region_.data()->data[1],
        owned_value,
        begin_offset + 4096,
        end_offset + 4096);
    EXPECT_TRUE(perm_fd >= 0);
    mapped_ptr = mmap(NULL,
                      end_offset - begin_offset,
                      PROT_WRITE | PROT_READ,
                      MAP_SHARED,
                      perm_fd,
                      0);
    EXPECT_FALSE(mapped_ptr == MAP_FAILED);
    EXPECT_FALSE(strcmp(reinterpret_cast<char*>(mapped_ptr), str.c_str()) == 0);
  }
  ManagedRegionTest() {}

 private:
  vsoc::ManagerRegionView<E2EManagerTestRegionLayout> manager_region_;
};

TEST(ManagedRegionTest, ManagedRegionFailMap) {
  ManagedRegionTest test;
  EXPECT_EXIT(test.testManagedRegionFailMap(),
              testing::ExitedWithCode(2),
              ".*" DEATH_TEST_MESSAGE ".*");
}

TEST(ManagedRegionTest, ManagedRegionMap) {
  ManagedRegionTest test;
  test.testManagedRegionMap();
}

int main(int argc, char** argv) {
  android::base::InitLogging(argv);
  testing::InitGoogleTest(&argc, argv);
  int rval = RUN_ALL_TESTS();
  if (!rval) {
    vsoc::TypedRegionView<E2EPrimaryTestRegionLayout> region;
    region.Open();
    E2EPrimaryTestRegionLayout* r = region.data();
    r->guest_status.set_value(vsoc::layout::e2e_test::E2E_MEMORY_FILLED);
    LOG(INFO) << "stage_1_guest_region_e2e_tests PASSED";
  }
  return rval;
}
