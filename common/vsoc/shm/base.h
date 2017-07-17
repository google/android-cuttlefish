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

#define ASSERT_SHM_COMPATIBLE(T, R)                                   \
  static_assert(sizeof(T) == vsoc::layout::version_info::R::T##_size, \
                "size changed, update the version");                  \
  static_assert(std::is_trivial<T>(), "Class uses features that are unsafe")

#define ASSERT_SHM_CONSTANT_VALUE(T, R)                                 \
  static_assert(T == vsoc::layout::version_info::R::constant_values::T, \
                "Constant value changed")

namespace vsoc {
namespace layout {
class Base {
 public:
};
ASSERT_SHM_COMPATIBLE(Base, multi_region);

}  // namespace layout
}  // namespace vsoc
