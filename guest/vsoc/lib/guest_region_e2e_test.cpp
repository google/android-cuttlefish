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

#include "common/vsoc/lib/e2e_test_region_view.h"
// TODO(b/64462568) Move the manager tests to a separate target
#include "guest/vsoc/lib/manager_region_view.h"

#include <android-base/logging.h>
#include <gtest/gtest.h>

#define DEATH_TEST_MESSAGE "abort converted to exit of 2 during death test"

using vsoc::layout::e2e_test::E2EManagedTestRegionLayout;
using vsoc::layout::e2e_test::E2EManagerTestRegionLayout;

static inline void disable_tombstones() {
  // We don't want a tombstone, and we're already in the child, so we modify the
  // behavior of LOG(ABORT) to print the well known message and do an
  // error-based exit.
  android::base::SetAborter([](const char*) {
    fputs(DEATH_TEST_MESSAGE, stderr);
    fflush(stderr);
    exit(2);
  });
}

template <typename View>
void DeathTestView(View *r) {
  disable_tombstones();
  // region.Open should never return.
  EXPECT_FALSE(r->Open());
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
// 10. Confirm that no interrupt is pending in the first region
// 11. Confirm that no interrupt is pending in the second region

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
  EXPECT_LE(2, num_data);
  for (size_t i = 0; i < num_data; ++i) {
    EXPECT_STREQ(View::Layout::host_pattern, in->host_string(i));
  }
}

TEST(RegionTest, BasicPeerTests) {
  vsoc::E2EPrimaryRegionView primary;
  vsoc::E2ESecondaryRegionView secondary;
  ASSERT_TRUE(primary.Open());
  ASSERT_TRUE(secondary.Open());
  LOG(INFO) << "Regions are open";
  SetGuestStrings(&primary);
  LOG(INFO) << "Primary guest strings are set";
  EXPECT_FALSE(secondary.HasIncomingInterrupt());
  LOG(INFO) << "Verified no early second interrupt";
  EXPECT_TRUE(primary.MaybeInterruptPeer());
  LOG(INFO) << "Interrupt sent. Waiting for first interrupt from peer";
  primary.WaitForInterrupt();
  LOG(INFO) << "First interrupt received";
  CheckPeerStrings(&primary);
  LOG(INFO) << "Verified peer's primary strings";
  SetGuestStrings(&secondary);
  LOG(INFO) << "Secondary guest strings are set";
  EXPECT_TRUE(secondary.MaybeInterruptPeer());
  LOG(INFO) << "Second interrupt sent";
  secondary.WaitForInterrupt();
  LOG(INFO) << "Second interrupt received";
  CheckPeerStrings(&secondary);
  LOG(INFO) << "Verified peer's secondary strings";
  EXPECT_FALSE(primary.HasIncomingInterrupt());
  EXPECT_FALSE(secondary.HasIncomingInterrupt());
  LOG(INFO) << "PASS: BasicPeerTests";
}

TEST(RegionTest, MissingRegionDeathTest) {
  vsoc::E2EUnfindableRegionView test;
  // EXPECT_DEATH creates a child for the test, so we do it out here.
  // DeathTestGuestRegion will actually do the deadly call after ensuring
  // that we don't create an unwanted tombstone.
  EXPECT_EXIT(DeathTestView(&test), testing::ExitedWithCode(2),
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
    const uint32_t owned_value = 65, begin_offset = 4096, end_offset = 8192;
    int perm_fd = manager_region_.CreateFdScopedPermission(
        &manager_region_.data()->data[0], owned_value, begin_offset,
        end_offset);
    EXPECT_TRUE(perm_fd >= 0);
    fd_scoped_permission perm;
    ASSERT_TRUE(ioctl(perm_fd, VSOC_GET_FD_SCOPED_PERMISSION, &perm) == 0);
    void* mapped_ptr = mmap(NULL, perm.end_offset - perm.begin_offset,
                            PROT_WRITE | PROT_READ, MAP_SHARED, perm_fd, 0);
    EXPECT_FALSE(mapped_ptr == MAP_FAILED);

    // Owned value gets written
    EXPECT_TRUE(manager_region_.data()->data[0] == owned_value);

    // Data written to the mapped memory stays there after unmap
    std::string str = "managed by e2e_manager";
    strcpy(reinterpret_cast<char*>(mapped_ptr), str.c_str());
    EXPECT_TRUE(munmap(mapped_ptr, end_offset - begin_offset) == 0);
    mapped_ptr = mmap(NULL, end_offset - begin_offset, PROT_WRITE | PROT_READ,
                      MAP_SHARED, perm_fd, 0);
    EXPECT_FALSE(mapped_ptr == MAP_FAILED);
    EXPECT_TRUE(strcmp(reinterpret_cast<char*>(mapped_ptr), str.c_str()) == 0);

    // Create permission elsewhere in the region, map same offset and length,
    // ensure data isn't there
    EXPECT_TRUE(munmap(mapped_ptr, end_offset - begin_offset) == 0);
    close(perm_fd);
    EXPECT_TRUE(manager_region_.data()->data[0] == 0);
    perm_fd = manager_region_.CreateFdScopedPermission(
        &manager_region_.data()->data[1], owned_value, begin_offset + 4096,
        end_offset + 4096);
    EXPECT_TRUE(perm_fd >= 0);
    mapped_ptr = mmap(NULL, end_offset - begin_offset, PROT_WRITE | PROT_READ,
                      MAP_SHARED, perm_fd, 0);
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
    vsoc::E2EPrimaryRegionView region;
    region.Open();
    region.guest_status(vsoc::layout::e2e_test::E2E_MEMORY_FILLED);
    LOG(INFO) << "stage_1_guest_region_e2e_tests PASSED";
  }
  return rval;
}
