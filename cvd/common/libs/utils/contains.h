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
#include <iterator>
#include <type_traits>

/**
 * @file: Implement Contains(container, key)
 *
 * The function returns true if container has the key, or false.
 *
 * If the container has a find(key) method (e.g. set, unordered_set, std::map,
 * etc), the find method is used. Otherwise, the std::find function from
 * algorithm is used, which may result in a linear search.
 *
 */
namespace cuttlefish {
namespace contains_internal_impl {

template <typename Container, typename Key>
using CheckFindMethodType =
    decltype(void(std::declval<Container&>().find(std::declval<Key&>())));

template <typename Container, typename T, typename = void>
struct HasFindImpl : std::false_type {};

template <typename Container, typename T>
struct HasFindImpl<Container, T, CheckFindMethodType<Container, T>>
    : std::true_type {};

}  // namespace contains_internal_impl

// TODO(kwstephenkim): Replace these when C++20 starts to be used.
template <typename Container, typename U,
          typename = std::enable_if_t<
              contains_internal_impl::HasFindImpl<Container, U>::value, void>>
constexpr bool Contains(Container&& container, U&& u) {
  // using O(1) or O(lgN) find()
  return container.find(std::forward<U>(u)) != container.end();
}

template <
    typename Container, typename U,
    std::enable_if_t<!contains_internal_impl::HasFindImpl<Container, U>::value,
                     int> = 0>
constexpr bool Contains(Container&& container, U&& u) {
  // falls back to a generic, likely linear search
  const auto itr =
      std::find(std::begin(container), std::end(container), std::forward<U>(u));
  return itr != std::end(container);
}

}  // namespace cuttlefish
