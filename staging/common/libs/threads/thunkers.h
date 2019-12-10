/*
 * Copyright (C) 2016 The Android Open Source Project
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
#ifndef CUTTLEFISH_COMMON_COMMON_LIBS_THREADS_THUNKERS_H_
#define CUTTLEFISH_COMMON_COMMON_LIBS_THREADS_THUNKERS_H_

namespace cvd {
namespace internal {

template <typename HalType, typename F>
struct ThunkerImpl;

template <typename HalType, typename Impl, typename R, typename... Args>
struct ThunkerImpl<HalType, R (Impl::*)(Args...)> {
  template <R (Impl::*MemFn)(Args...)>
  static R call(HalType* in, Args... args) {
    return (reinterpret_cast<Impl*>(in)->*MemFn)(args...);
  }
};

template <typename HalType, typename Impl, typename R, typename... Args>
struct ThunkerImpl<HalType, R (Impl::*)(Args...) const> {
  template <R (Impl::*MemFn)(Args...) const>
  static R call(const HalType* in, Args... args) {
    return (reinterpret_cast<const Impl*>(in)->*MemFn)(args...);
  }
};

template <typename HalType, auto MemFunc>
struct Thunker {
  static constexpr auto call =
      ThunkerImpl<HalType, decltype(MemFunc)>::template call<MemFunc>;
};

}  // namespace internal

template <typename HalType, auto MemFunc>
constexpr auto thunk = internal::Thunker<HalType, MemFunc>::call;

}  // namespace cvd

#endif
