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

#include "cuttlefish/pretty/struct.h"

#include <ostream>
#include <string_view>

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"

namespace cuttlefish {

PrettyStruct::PrettyStruct(std::string_view name) : name_(name) {}

PrettyStruct& PrettyStruct::Member(std::string_view name,
                                   std::string_view value) & {
  MemberInternal(absl::StrCat(name, ": \"", value, "\""));
  return *this;
}

PrettyStruct PrettyStruct::Member(std::string_view name,
                                  std::string_view value) && {
  Member(name, value);
  return *this;
}

PrettyStruct& PrettyStruct::Member(std::string_view name, const char* value) & {
  Member(name, std::string_view(value));
  return *this;
}

PrettyStruct PrettyStruct::Member(std::string_view name, const char* value) && {
  Member(name, std::string_view(value));
  return *this;
}

PrettyStruct& PrettyStruct::Member(std::string_view name,
                                   const std::string& value) & {
  Member(name, std::string_view(value));
  return *this;
}

PrettyStruct PrettyStruct::Member(std::string_view name,
                                  const std::string& value) && {
  Member(name, std::string_view(value));
  return *this;
}

void PrettyStruct::MemberInternal(std::string_view line) {
  members_.emplace_back(absl::StrReplaceAll(line, {{"\n", "\n  "}}));
}

std::ostream& operator<<(std::ostream& out, const PrettyStruct& ps) {
  return out << absl::StreamFormat("%v", ps);
}

// For libfmt
std::string format_as(const PrettyStruct& ps) { return absl::StrCat(ps); }

}  // namespace cuttlefish
