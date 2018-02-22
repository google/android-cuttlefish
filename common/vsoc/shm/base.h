#pragma once
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

// Base macros for all layout structures.

#include <type_traits>
#include "common/vsoc/shm/version.h"

// ShmTypeValidator provides meaningful information about the type size
// mismatch in compilation error messages, eg.
//
// error:
//    static_assert failed "Class size changed, update the version"
//    static_assert(Current == Expected,
// note: in instantiation of template class
//    'ShmTypeValidator<vsoc::layout::myclass::ClassName, 1232, 1240>'
//    requested here ASSERT_SHM_COMPATIBLE(ClassName, myclass);
//
template<typename Type, size_t Current, size_t Expected>
struct ShmTypeValidator {
    static_assert(Current == Expected,
                  "Class size changed, update the version");
    static_assert(std::is_trivial<Type>(),
                  "Class uses features that are unsafe");
    static constexpr bool valid = (Current == Expected);
};

#define ASSERT_SHM_COMPATIBLE(T, R)                                   \
  static_assert(                                                      \
      ShmTypeValidator<T, sizeof(T), vsoc::layout::version_info::R::T##_size> \
      ::valid, "Compilation error. Please fix above errors and retry.")

#define ASSERT_SHM_CONSTANT_VALUE(T, R)                                 \
  static_assert(T == vsoc::layout::version_info::R::constant_values::T, \
                "Constant value changed")

namespace vsoc {
namespace layout {

/**
 * Memory is shared between Guest and Host kernels. In some cases we need
 * flag to indicate which side we're on. In those cases we'll use this
 * simple structure.
 *
 * These are carefully formatted to make Guest and Host a bitfield.
 */
enum Sides : uint32_t {
  NoSides = 0,
  Guest = 1,
  Host = 2,
  Both = 3,
#ifdef CUTTLEFISH_HOST
  OurSide = Host,
  Peer = Guest
#else
  OurSide = Guest,
  Peer = Host
#endif
};
ASSERT_SHM_COMPATIBLE(Sides, multi_region);

/**
 * Base class for all region layout structures.
 */
class RegionLayout {
};
ASSERT_SHM_COMPATIBLE(RegionLayout, multi_region);

}  // namespace layout
}  // namespace vsoc
