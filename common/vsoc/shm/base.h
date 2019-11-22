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

#include <stdint.h>
#include <type_traits>

// Base macros for all layout structures.
// ShmTypeValidator provides meaningful information about the type size
// mismatch in compilation error messages, eg.
//
// error:
//    static_assert failed "Class size changed, update the layout_size field"
//    static_assert(Current == Expected,
// note: in instantiation of template class
//    'ShmTypeValidator<vsoc::layout::myclass::ClassName>'
//    requested here ASSERT_SHM_COMPATIBLE(ClassName);
//
template <typename Type, size_t expected_size = Type::layout_size>
struct ShmTypeValidator {
  static_assert(sizeof(Type) == expected_size,
                "Class size changed, update the layout_size field");
  static_assert(std::is_trivial<Type>(), "Class uses features that are unsafe");
  static constexpr bool valid =
      sizeof(Type) == expected_size && std::is_trivial<Type>();
};

#define ASSERT_SHM_COMPATIBLE(T)            \
  static_assert(ShmTypeValidator<T>::valid, \
                "Compilation error. Please fix above errors and retry.")

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
// Enums can't have static members, so can't use the macro here.
  static_assert(ShmTypeValidator<Sides, 4>::valid,
              "Compilation error. Please fix above errors and retry.");

/**
 * Base class for all region layout structures.
 */
class RegionLayout {
public:
  static constexpr size_t layout_size = 1;
};
ASSERT_SHM_COMPATIBLE(RegionLayout);

}  // namespace layout
}  // namespace vsoc
