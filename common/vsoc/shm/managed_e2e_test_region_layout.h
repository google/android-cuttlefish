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

#include <cstdint>

#include "common/vsoc/shm/base.h"

namespace vsoc {
namespace layout {

namespace e2e_test {

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
