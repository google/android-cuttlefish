//
// Copyright (C) 2019 The Android Open Source Project
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

#include <optional>
#include <vector>

namespace cuttlefish {

template <typename T>
std::optional<T> GetOptional(const std::vector<T>& vector, size_t i) {
  return i < vector.size() ? std::optional(vector[i]) : std::nullopt;
}

template <typename T>
std::optional<T> GetOptional(const std::vector<std::optional<T>>& vector,
                             size_t i) {
  return i < vector.size() ? vector[i] : std::nullopt;
}

}  // namespace cuttlefish
