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

#include "cuttlefish/pretty/container.h"

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_replace.h"

namespace cuttlefish {

void PrettyContainerType::MemberInternal(std::string_view line) {
  members_.emplace_back(absl::StrReplaceAll(line, {{"\n", "\n  "}}));
}

std::ostream& operator<<(std::ostream& out, const PrettyContainerType& pc) {
  return out << absl::StreamFormat("%v", pc);
}

// For libfmt
std::string format_as(const PrettyContainerType& pc) {
  return absl::StrCat(pc);
}

}  // namespace cuttlefish
