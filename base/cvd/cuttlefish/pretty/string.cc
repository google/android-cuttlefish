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

#include "cuttlefish/pretty/string.h"

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "cuttlefish/pretty/pretty.h"

namespace cuttlefish {

// TODO: schuffelen - escape inner special characters
std::string Pretty(const std::string_view& value, PrettyAdlPlaceholder) {
  return absl::StrCat("\"", value, "\"");
}

std::string Pretty(const char* const& value, PrettyAdlPlaceholder) {
  return Pretty(std::string_view(value));
}

std::string Pretty(const std::string& value, PrettyAdlPlaceholder) {
  return Pretty(std::string_view(value));
}

}  // namespace cuttlefish
