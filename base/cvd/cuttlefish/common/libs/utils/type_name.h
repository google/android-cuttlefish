/*
 * Copyright (C) 2023 The Android Open Source Project
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
#pragma once

#include <cstdlib>
#include <string_view>

namespace cuttlefish {

namespace internal {

template <typename T>
struct CompileTimeTypeName {
  static constexpr std::string_view PrettyFn() { return __PRETTY_FUNCTION__; }
};

template <auto T>
struct CompileTimeValueName {
  static constexpr std::string_view PrettyFn() { return __PRETTY_FUNCTION__; }
};

constexpr std::string_view ExtractName(std::string_view name) {
  std::string_view value_prefix = "internal::CompileTimeValueName<";
  if (auto begin = name.find(value_prefix); begin != std::string_view::npos) {
    name = name.substr(begin + value_prefix.size());
  }

  std::string_view type_prefix = "internal::CompileTimeTypeName<";
  if (auto begin = name.find(type_prefix); begin != std::string_view::npos) {
    name = name.substr(begin + type_prefix.size());
  }

  if (name.size() > 0 && name[0] == '&') {
    name = name.substr(1);
  }

  constexpr std::string_view suffix = ">::PrettyFn";
  if (auto begin = name.rfind(suffix); begin != std::string_view::npos) {
    name = name.substr(0, begin);
  }

  return name;
}

}  // namespace internal

template <typename T>
inline constexpr std::string_view TypeName() {
  return internal::ExtractName(internal::CompileTimeTypeName<T>::PrettyFn());
}

static_assert(TypeName<int>() == "int");

template <auto T>
inline constexpr std::string_view ValueName() {
  return internal::ExtractName(internal::CompileTimeValueName<T>::PrettyFn());
}

// static_assert(ValueName<5>() == "5");

}  // namespace cuttlefish
