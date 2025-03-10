//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <inttypes.h>

#include <android-base/endian.h>

// The utilities in android-base/endian.h still require the use of regular int
// types to store values with any endianness, which requires the user to
// remember to manually do the required conversions, which is prone to errors.
// The types introduced here allow handling these values safely.

namespace cuttlefish {

#define DECLARE_TYPE(new_type, base_type, to_new, to_base)                \
  class new_type {                                                        \
   public:                                                                \
    new_type() = default;                                                 \
    explicit new_type(base_type val) : inner_(to_new(val)) {}             \
    new_type(const new_type&) = default;                                  \
    new_type& operator=(const new_type& other) = default;                 \
    volatile new_type& operator=(const new_type& other) volatile {        \
      inner_ = other.inner_;                                              \
      return *this;                                                       \
    }                                                                     \
    base_type as_##base_type() const volatile { return to_base(inner_); } \
                                                                          \
   private:                                                               \
    base_type inner_;                                                     \
  };                                                                      \
  static_assert(sizeof(new_type) == sizeof(base_type))

DECLARE_TYPE(Le16, uint16_t, htole16, le16toh);
DECLARE_TYPE(Le32, uint32_t, htole32, le32toh);
DECLARE_TYPE(Le64, uint64_t, htole64, le64toh);
DECLARE_TYPE(Be16, uint16_t, htobe16, be16toh);
DECLARE_TYPE(Be32, uint32_t, htobe32, be32toh);
DECLARE_TYPE(Be64, uint64_t, htobe64, be64toh);

#undef DECLARE_TYPE

}  // namespace cuttlefish