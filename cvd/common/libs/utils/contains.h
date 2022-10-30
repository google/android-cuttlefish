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
 * If the container has a find(key) method (e.g. set, unordered_set, etc), the
 * find method is used. Otherwise, the std::find function from algorithm is
 * used, which may result in a linear search.
 */
namespace cuttlefish {
namespace contains_internal_impl {

template <typename Container>
using ElemType = decltype(*(std::cbegin(std::declval<Container&>())));

template <typename Container>
using CheckFindMethodType = decltype(void(
    std::declval<Container&>().find(std::declval<ElemType<Container>&>())));

template <typename Container, typename = void>
struct HasFindImpl : std::false_type {};

template <typename Container>
struct HasFindImpl<Container, CheckFindMethodType<Container>> : std::true_type {
};

template <typename Container>
constexpr bool HasFind(Container&&) {
  return HasFindImpl<Container>::value;
}

}  // namespace contains_internal_impl

// TODO(kwstephenkim): Replace these when C++20 starts to be used.
template <typename Container, typename U,
          typename = std::enable_if_t<
              contains_internal_impl::HasFindImpl<Container>::value, void>>
constexpr bool Contains(Container&& container, U&& u) {
  // using O(1) or O(lgN) find()
  return container.find(std::forward<U>(u)) != container.end();
}

template <typename Container, typename U,
          std::enable_if_t<
              !contains_internal_impl::HasFindImpl<Container>::value, int> = 0>
constexpr bool Contains(Container&& container, U&& u) {
  // falls back to a generic, likely linear search
  const auto itr =
      std::find(std::begin(container), std::end(container), std::forward<U>(u));
  return itr != std::end(container);
}

}  // namespace cuttlefish
