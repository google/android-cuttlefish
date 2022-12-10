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
#include <string>
#include <string_view>
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
 * See go/cf-utils-contains for more details.
 */
namespace cuttlefish {
namespace contains_internal_impl {

/*
 * If Container does not have find key, will be a compiler error used
 * by SFINAE. If it does have one, this is equivalent to the "void" type.
 */
template <typename Container, typename Key>
using VoidTypeIfHasFind =
    decltype(void(std::declval<Container&>().find(std::declval<Key&>())));

/*
 * Here is how this works:
 *
 * Given that
 *   HasFindImpl<Container, T> is used in the code
 *
 *   1. The input is effectively regarded as HasFindImpl<Container, T, void>.
 *   The specialized version below isn't looked up yet; whether the specialized
 *   version below is used or not, the compiler front-end needs all three
 *   template parameters to match against either special or generic version.
 *   When obtaining "all three," the front-end only looks up the base template
 *   definition. The default type of the third template parameter is void, so
 *   the given type is expanded/deduced to HasFindImpl<Container, T, void>.
 *
 *   2. Now, given HasFindImpl<Container, T, void>, the compiler front-end
 *   tries matching against the specialized and generic/original versions. If
 *   the input could matches both a generic and a specialized one, the compiler
 *   chooses the specialized one. Thus, particularly, HasFindImpl
 *   implementation's third parameter in the specialized version must be the
 *   same as the default type of the third template parameter to the original/
 *   generic version, which is "void."
 */
template <typename Container, typename T, typename = void>
struct HasFindImpl : std::false_type {};

template <typename Container, typename T>
struct HasFindImpl<Container, T, VoidTypeIfHasFind<Container, T>>
    : std::true_type {};

template <typename T>
using RemoveCvref =
    typename std::remove_cv_t<typename std::remove_reference_t<T>>;

template <typename T, typename U>
using IsSame = typename std::is_same<RemoveCvref<T>, RemoveCvref<U>>;

template <typename T>
struct IsString : IsSame<std::string, T> {};

template <typename T>
struct IsStringView : IsSame<std::string_view, T> {};

}  // namespace contains_internal_impl

// TODO(kwstephenkim): Replace these when C++20 starts to be used.
template <typename Container, typename U,
          typename = std::enable_if_t<
              contains_internal_impl::HasFindImpl<Container, U>::value &&
                  (!contains_internal_impl::IsString<Container>::value &&
                   !contains_internal_impl::IsStringView<Container>::value),
              void>>
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

// std::string:: or std::string_view::find() returns index, not iterator
template <typename T>
constexpr bool Contains(const std::string& s, T&& t) {
  return s.find(std::forward<T>(t)) != std::string::npos;
}

template <typename T>
constexpr bool Contains(const std::string_view& s, T&& t) {
  return s.find(std::forward<T>(t)) != std::string_view::npos;
}

}  // namespace cuttlefish
