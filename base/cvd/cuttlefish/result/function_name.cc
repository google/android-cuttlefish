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

#include "cuttlefish/result/function_name.h"

#include <stddef.h>

#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"

namespace cuttlefish {
namespace {

/* Erase a nesting grouping character pair like `(())` or `<<>>`. */
std::string RemovePairedCharacters(std::string input, char begin_char,
                                   char end_char) {
  size_t depth = 0;
  std::string::iterator begin_it = input.begin();
  for (auto it = input.begin(); it != input.end();) {
    if (*it == begin_char) {
      if (depth == 0) {
        begin_it = it;
      }
      depth++;
    } else if (*it == end_char) {
      if (depth == 0) {
        it++;
        continue;
      }
      depth--;
      if (depth == 0) {
        it++;
        it = input.erase(begin_it, it);
        continue;
      }
    }
    it++;
  }
  if (depth > 0) {
    input.erase(begin_it, input.end());
  }
  return input;
}

}  // namespace

std::string FunctionName(std::string function_signature) {
  std::string name = std::move(function_signature);
  name = RemovePairedCharacters(std::move(name), '(', ')');
  name = RemovePairedCharacters(std::move(name), '<', '>');

  std::vector<std::string> parts = absl::StrSplit(name, " ");
  const static std::set<std::string>* kBadParts =
      new std::set<std::string>{"const", "volatile", "override"};
  std::erase_if(parts,
                [](const std::string& part) { return kBadParts->count(part); });

  std::string last_component =
      parts.empty() ? "" : std::move(parts[parts.size() - 1]);
  const size_t last_colon = last_component.find_last_of(":");
  if (last_colon != std::string::npos) {
    last_component.erase(0, last_colon + 1);
  }
  return last_component;
}

std::string FunctionName(const char* function_signature) {
  return FunctionName(std::string(function_signature));
}

std::string FunctionName(std::string_view function_signature) {
  return FunctionName(std::string(function_signature));
}

std::string FunctionName(std::source_location location) {
  return FunctionName(location.function_name());
}

}  // namespace cuttlefish
