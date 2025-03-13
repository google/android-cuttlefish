/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <incfs_support/signal_handling.h>

#include <optional>
#include <type_traits>

namespace incfs {

// Couple helper templates for access() implementation
template <class Arg, class Func>
using func_result =
    std::remove_cvref_t<decltype(std::declval<Func>()(std::declval<Arg>()))>;

template <class Arg, class Func>
constexpr auto is_void_func = std::is_same_v<func_result<Arg, Func>, void>;

template <class Arg, class Func>
using optional_result =
    std::conditional_t<is_void_func<Arg, Func>, bool,
                       std::optional<func_result<Arg, Func>>>;

//
// A safer alternative for the SCOPED_SIGBUS_HANDLER() macro.
//
// This function accepts a closure, which is a block of code that may generate
// an error while accessing mapped memory. By using access() function you limit
// the potential for leaking resources to only the code in the closure. Anything
// allocated before, or after the call, works exactly as expected by the C++
// standards, and will get its destructors run even in case of access error. But
// the code inside the closure still has the same issues as with the macros - no
// cleanup happens if reading memory fails. That's why it's better to not create
// any new objects inside of it, and not call external code, especially the one
// you don't control.
//
// While it's a safer alternative, one may still need to use the functions from
// incfs_support/util.h to ensure a proper memory cleanup.
//
// Code generated from the access() call is very similar to the raw macro, but
// sometimes the optimizer decides to make accessor() a real function call
// instead of inlining it. This is the only piece of overhead you may get, and
// that's why best practice is to start with this function first. Only fall back
// to SCOPED_SIGBUS_HANDLER() macro if it gives MEASURABLE improvement.
//
// @param ptr       - mapped memory pointer to access
// @param accessor  - a closure that runs under failure protection. Its return
// value wrapped into
//                    std::optional is the whole function return value
// @return          - std::optional(access(ptr)) if no error happened, or
// std::nullopt on error;
//                    for a void-returning accessor a bool indicating if access
//                    was successful.

template <class Ptr, class F>
auto access(Ptr ptr, F&& accessor) -> optional_result<Ptr, F> {
  SCOPED_SIGBUS_HANDLER({ return {}; });

  if constexpr (is_void_func<Ptr, F>) {
    accessor(ptr);
    return true;
  } else {
    return accessor(ptr);
  }
}

}  // namespace incfs