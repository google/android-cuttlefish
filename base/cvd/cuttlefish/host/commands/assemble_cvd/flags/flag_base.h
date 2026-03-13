/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <stddef.h>

#include <utility>
#include <vector>

namespace cuttlefish {

template <typename T>
class FlagBase {
 public:
  T ForIndex(const size_t index) const {
    if (index < values_.size()) {
      return values_[index];
    } else {
      return values_[0];
    }
  }

  bool IsDefault() const { return is_default_; }

  size_t Size() const { return values_.size(); }
  const std::vector<T>& AsVector() const { return values_; }

 protected:
  explicit FlagBase(std::vector<T> flag_values, bool is_default)
      : values_(std::move(flag_values)), is_default_(is_default) {}
  virtual ~FlagBase() = 0;

 private:
  std::vector<T> values_;
  bool is_default_;
};

template <typename T>
FlagBase<T>::~FlagBase() {}

extern template class FlagBase<bool>;
extern template class FlagBase<int>;
extern template class FlagBase<std::string>;

}  // namespace cuttlefish
