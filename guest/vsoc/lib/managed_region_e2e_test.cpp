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

#include "common/vsoc/shm/managed_e2e_test_region_layout.h"
#include "guest/vsoc/lib/e2e_test_common.h"
#include "guest/vsoc/lib/manager_region_view.h"

#include <android-base/logging.h>
#include <gtest/gtest.h>

using vsoc::layout::e2e_test::E2EManagedTestRegionLayout;
using vsoc::layout::e2e_test::E2EManagerTestRegionLayout;

// Region view classes to allow calling the Open() function from the test.
class E2EManagedTestRegionView
    : public vsoc::TypedRegionView<
        E2EManagedTestRegionView,
        E2EManagedTestRegionLayout> {
 public:
  using vsoc::TypedRegionView<
      E2EManagedTestRegionView, E2EManagedTestRegionLayout>::Open;
};
class E2EManagerTestRegionView
    : public vsoc::ManagerRegionView<
        E2EManagerTestRegionView,
        E2EManagerTestRegionLayout> {
 public:
  using vsoc::ManagerRegionView<
      E2EManagerTestRegionView, E2EManagerTestRegionLayout>::Open;
};

class ManagedRegionTest {
 public:
  void testManagedRegionFailMap() {
    E2EManagedTestRegionView managed_region;
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
  E2EManagerTestRegionView manager_region_;
};

TEST(ManagedRegionTest, ManagedRegionFailMap) {
  ManagedRegionTest test;
  EXPECT_EXIT(test.testManagedRegionFailMap(), testing::ExitedWithCode(2),
              ".*" DEATH_TEST_MESSAGE ".*");
}

TEST(ManagedRegionTest, ManagedRegionMap) {
  ManagedRegionTest test;
  test.testManagedRegionMap();
}

int main(int argc, char** argv) {
  android::base::InitLogging(argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
