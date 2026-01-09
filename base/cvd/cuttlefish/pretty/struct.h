//
// Copyright (C) 2026 The Android Open Source Project
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

#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

namespace cuttlefish {

/**
 * Creates a "formatted struct", comparable to Rust's std::fmt::DebugStruct.
 *
 * Supports abseil string formatting, libfmt, and ostreams.
 *
 * Example usage:
 * ```
 * const PrettyStruct inner =
 *     PrettyStruct("Inner").Member("i1", 1).Member("i2", 2);
 * const PrettyStruct outer =
 *     PrettyStruct("Outer").Member("o1", inner).Member("o2", inner);
 * ```
 * formats as
 * ```
 * Outer {
 *   o1: Inner {
 *     i1: 1,
 *     i2: 2
 *   },
 *   o2: Inner {
 *     i1: 1,
 *     i2: 2
 *   }
 * }
 * ```
 */
class PrettyStruct {
 public:
  explicit PrettyStruct(std::string_view name);

  template <typename T>
  PrettyStruct& Member(std::string_view name, const T& value) & {
    MemberInternal(absl::StrCat(name, ": ", value));
    return *this;
  }

  template <typename T>
  PrettyStruct Member(std::string_view name, const T& value) && {
    this->Member(name, value);
    return std::move(*this);
  }

  // String members are quoted
  PrettyStruct& Member(std::string_view name, std::string_view value) &;
  PrettyStruct Member(std::string_view name, std::string_view value) &&;
  PrettyStruct& Member(std::string_view name, const char* value) &;
  PrettyStruct Member(std::string_view name, const char* value) &&;
  PrettyStruct& Member(std::string_view name, const std::string& value) &;
  PrettyStruct Member(std::string_view name, const std::string& value) &&;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const PrettyStruct& ps) {
    if (ps.members_.empty()) {
      absl::Format(&sink, "%v {}", ps.name_);
    } else {
      absl::Format(&sink, "%v {\n  %v\n}", ps.name_,
                   absl::StrJoin(ps.members_, ",\n  "));
    }
  }

 private:
  void MemberInternal(std::string_view);

  std::string name_;
  std::vector<std::string> members_;
};

std::ostream& operator<<(std::ostream&, const PrettyStruct&);

// For libfmt
std::string format_as(const PrettyStruct&);

}  // namespace cuttlefish
