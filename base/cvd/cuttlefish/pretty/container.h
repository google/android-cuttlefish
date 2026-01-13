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
#include <type_traits>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "cuttlefish/pretty/pretty.h"
#include "cuttlefish/pretty/string.h"  // IWYU pragma: export

namespace cuttlefish {

/**
 * Pretty-prints a container. Invoke this using the `PrettyContainer` overloads.
 *
 * Example output:
 * ```
 * PrettyContainer(std::vector<int>{1, 2})
 * ```
 * formats as
 * ```
 * {
 *   1,
 *   2
 * }
 * ```
 *
 */
class PrettyContainerType {
 public:
  template <typename T, typename FmtMemberFn>
  friend PrettyContainerType PrettyContainer(const T& container,
                                             FmtMemberFn format_member_fn);

  template <typename T>
  friend PrettyContainerType PrettyContainer(const T& container);

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const PrettyContainerType& pc) {
    if (pc.members_.empty()) {
      sink.Append("{}");
    } else {
      absl::Format(&sink, "{\n  %v\n}", absl::StrJoin(pc.members_, ",\n  "));
    }
  }

 private:
  PrettyContainerType() = default;

  void MemberInternal(std::string_view);

  std::vector<std::string> members_;
};

template <typename T, typename FmtMemberFn>
PrettyContainerType PrettyContainer(const T& container,
                                    FmtMemberFn format_member_fn) {
  PrettyContainerType pretty;
  for (const auto& member : container) {
    pretty.MemberInternal(absl::StrCat(format_member_fn(member)));
  }
  return pretty;
}

template <typename T>
PrettyContainerType PrettyContainer(const T& container) {
  return PrettyContainer(container, [](const auto& member) {
    return Pretty(member, PrettyAdlPlaceholder());
  });
}

std::ostream& operator<<(std::ostream& out, const PrettyContainerType&);

// For libfmt
std::string format_as(const PrettyContainerType& pc);

}  // namespace cuttlefish
