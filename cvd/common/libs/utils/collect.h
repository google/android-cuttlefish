/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <algorithm>

#include "common/libs/utils/result.h"

namespace cuttlefish {

/**
 * return all the elements in container that satisfies predicate.
 *
 * Container could be mostly any type, and Set should be any sort of set.
 */
template <typename T, typename Set, typename Container>
Set Collect(const Container& container,
            std::function<bool(const T&)> predicate) {
  Set output;
  std::copy_if(container.cbegin(), container.cend(),
               std::inserter(output, output.end()), predicate);
  return output;
}

/**
 * Collect all Ts from each container inside the "Containers"
 *
 * Containers are a set/list of Container. Container can be viewed as a set/list
 * of Ts.
 *
 */
template <typename T, typename Set, typename Containers>
Set Flatten(const Containers& containers) {
  Set output;
  for (const auto& container : containers) {
    output.insert(container.cbegin(), container.cend());
  }
  return output;
}

template <typename S>
Result<typename std::remove_reference<S>::type> AtMostN(S&& s, const size_t n) {
  CF_EXPECT(s.size() <= n);
  return {std::forward<S>(s)};
}

}  // namespace cuttlefish
