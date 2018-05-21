#pragma once
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

#include <atomic>
#include <cstdint>
#include "common/vsoc/shm/base.h"

// Memory layout for a region that supports end-to-end (E2E) testing of
// shared memory regions. This verifies that all sorts of things work along the
// the path:
//
//   host libraries <-> ivshmem server <-> kernel <-> guest libraries
//
// This is intentionally not a unit test. The primary source of errors along
// this path is a misunderstanding and/or inconsistency in one of the
// interfaces. Introducing mocks would allow these errors to go undetected.
// Another way of looking at it is that the mocks would end up being a
// a copy-and-paste job, making a series of change-detector tests.
//
// These tests are actually run on every device boot to verify that things are
// ok.

namespace vsoc {
namespace layout {

namespace e2e_test {

/**
 * Flags that are used to indicate test status. Some of the latter testing
 * stages rely on initializion that must be done on the peer.
 */
  enum E2ETestStage : uint32_t {
  // No tests have passed
  E2E_STAGE_NONE = 0,
  // This side has finished writing its pattern to the region
  E2E_MEMORY_FILLED = 1,
  // This side has confirmed that it can see its peer's writes to the region
  E2E_PEER_MEMORY_READ = 2,
};
static_assert(ShmTypeValidator<E2ETestStage, 4>::valid,
              "Compilation error. Please fix above errors and retry.");

/**
 * Structure that grants permission to write in the region to either the guest
 * or the host. This size of these fields is arbitrary.
 */
struct E2EMemoryFill {
  static constexpr size_t layout_size = 64;

  static const std::size_t kOwnedFieldSize = 32;

  // The compiler must not attempt to optimize away reads and writes to the
  // shared memory window. This is pretty typical when dealing with devices
  // doing memory mapped I/O.
  char host_writable[kOwnedFieldSize];
  char guest_writable[kOwnedFieldSize];
};
ASSERT_SHM_COMPATIBLE(E2EMemoryFill);

/**
 * Structure that grants permission to write in the region to either the guest
 * or the host. This size of these fields is arbitrary.
 */
class E2ETestStageRegister {
 public:
  static constexpr size_t layout_size = 4;

  E2ETestStage value() const {
    return value_;
  }

  void set_value(E2ETestStage new_value) { value_ = new_value; }

 protected:
  // The compiler must not attempt to optimize away reads and writes to the
  // shared memory window. This is pretty typical when dealing with devices
  // doing memory mapped I/O.
  E2ETestStage value_;
};
ASSERT_SHM_COMPATIBLE(E2ETestStageRegister);

/**
 * Describes the layout of the regions used for the end-to-end test. There
 * are multiple regions: primary and secondary, so some details like the region
 * name must wait until later.
 */
class E2ETestRegionLayout : public ::vsoc::layout::RegionLayout {
 public:
  static constexpr size_t layout_size = 2 * E2ETestStageRegister::layout_size +
                                        3 * 4 + E2EMemoryFill::layout_size;

  /**
   * Computes how many E2EMemoryFill records we need to cover the region.
   * Covering the entire region during the test ensures that everything is
   * mapped and coherent between guest and host.
   */
  static std::size_t NumFillRecords(std::size_t region_size) {
    if (region_size < sizeof(E2ETestRegionLayout)) {
      return 0;
    }
    // 1 + ... An array of size 1 is allocated in the E2ETestRegion.
    // TODO(ghartman): AddressSanitizer may find this sort of thing to be
    // alarming.
    return 1 +
           (region_size - sizeof(E2ETestRegionLayout)) / sizeof(E2EMemoryFill);
  }
  // The number of test stages that have completed on the guest
  // Later host tests will wait on this
  E2ETestStageRegister guest_status;
  // The number of test stages that have completed on the host
  // Later guest tests will wait on this
  E2ETestStageRegister host_status;
  // These fields are used to test the signaling mechanism.
  std::atomic<uint32_t> host_to_guest_signal;
  std::atomic<uint32_t> guest_to_host_signal;
  std::atomic<uint32_t> guest_self_register;
  // There rest of the region will be filled by guest_host_strings.
  // We actually use more than one of these, but we can't know how many
  // until we examine the region.
  E2EMemoryFill data[1];
};
ASSERT_SHM_COMPATIBLE(E2ETestRegionLayout);

struct E2EPrimaryTestRegionLayout : public E2ETestRegionLayout {
  static constexpr size_t layout_size = E2ETestRegionLayout::layout_size;

  static const char* region_name;
  static const char guest_pattern[E2EMemoryFill::kOwnedFieldSize];
  static const char host_pattern[E2EMemoryFill::kOwnedFieldSize];
};
ASSERT_SHM_COMPATIBLE(E2EPrimaryTestRegionLayout);

struct E2ESecondaryTestRegionLayout : public E2ETestRegionLayout {
  static constexpr size_t layout_size = E2ETestRegionLayout::layout_size;

  static const char* region_name;
  static const char guest_pattern[E2EMemoryFill::kOwnedFieldSize];
  static const char host_pattern[E2EMemoryFill::kOwnedFieldSize];
};
ASSERT_SHM_COMPATIBLE(E2ESecondaryTestRegionLayout);

/**
 * Defines an end-to-end region with a name that should never be configured.
 */
struct E2EUnfindableRegionLayout : public E2ETestRegionLayout {
  static constexpr size_t layout_size = E2ETestRegionLayout::layout_size;

  static const char* region_name;
};
ASSERT_SHM_COMPATIBLE(E2EUnfindableRegionLayout);

struct E2EManagedTestRegionLayout : public RegionLayout {
  static constexpr size_t layout_size = 4;

  static const char* region_name;
  uint32_t val;  // Not needed, here only to avoid an empty struct.
};
ASSERT_SHM_COMPATIBLE(E2EManagedTestRegionLayout);

struct E2EManagerTestRegionLayout : public RegionLayout {
  static constexpr size_t layout_size = 4 * 4;

  static const char* region_name;
  typedef E2EManagedTestRegionLayout ManagedRegion;
  uint32_t data[4];  // We don't need more than 4 for the tests
};
ASSERT_SHM_COMPATIBLE(E2EManagerTestRegionLayout);

}  // namespace e2e_test
}  // namespace layout
}  // namespace vsoc
