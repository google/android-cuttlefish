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

#include <functional>
#include <type_traits>
#include <variant>

namespace gfxstream {

template <typename E>
class unexpected;

template <class E>
unexpected(E) -> unexpected<E>;

#define ENABLE_IF(...) typename std::enable_if<__VA_ARGS__>::type* = nullptr

template <typename T, typename E>
class expected {
 public:
  constexpr expected() = default;
  constexpr expected(const expected& rhs) = default;
  constexpr expected(expected&& rhs) = default;

  template <typename CopyT = T, ENABLE_IF(!std::is_void<CopyT>::value)>
  constexpr expected(T&& v)
      : mVariant(std::in_place_index<0>, std::forward<T>(v)) {}

  template <class... Args,
            ENABLE_IF(std::is_constructible<T, Args&&...>::value)>
  constexpr expected(std::in_place_t, Args&&... args)
      : mVariant(std::in_place_index<0>, std::forward<Args>(args)...) {}

  constexpr expected(const unexpected<E>& u)
      : mVariant(std::in_place_index<1>, u.value()) {}

  template <class OtherE = E,
            ENABLE_IF(std::is_constructible<E, const OtherE&>::value)>
  constexpr expected(const unexpected<OtherE>& e)
      : mVariant(std::in_place_index<1>, e.value()) {}

  constexpr const T* operator->() const { return std::addressof(value()); }
  constexpr T* operator->() { return std::addressof(value()); }
  constexpr const T& operator*() const& { return value(); }
  constexpr T& operator*() & { return value(); }
  constexpr const T&& operator*() const&& {
    return std::move(std::get<T>(mVariant));
  }
  constexpr T&& operator*() && { return std::move(std::get<T>(mVariant)); }

  constexpr bool has_value() const { return mVariant.index() == 0; }
  constexpr bool ok() const { return has_value(); }

  template <typename T2 = T, ENABLE_IF(!std::is_void<T>::value)>
  constexpr const T& value() const& {
    return std::get<T>(mVariant);
  }
  template <typename T2 = T, ENABLE_IF(!std::is_void<T>::value)>
  constexpr T& value() & {
    return std::get<T>(mVariant);
  }

  constexpr const T&& value() const&& {
    return std::move(std::get<T>(mVariant));
  }
  constexpr T&& value() && { return std::move(std::get<T>(mVariant)); }

  constexpr const E& error() const& { return std::get<E>(mVariant); }
  constexpr E& error() & { return std::get<E>(mVariant); }
  constexpr const E&& error() const&& {
    return std::move(std::get<E>(mVariant));
  }
  constexpr E&& error() && { return std::move(std::get<E>(mVariant)); }

  template <typename F,
            typename NewE = std::remove_cv_t<std::invoke_result_t<F, E>>>
  constexpr expected<T, NewE> transform_error(F&& function) {
    if (ok()) {
      if constexpr (std::is_void_v<T>) {
        return expected<T, NewE>();
      } else {
        return expected<T, NewE>(std::in_place, value());
      }
    } else {
      return unexpected(std::invoke(std::forward<F>(function), error()));
    }
  }

 private:
  std::variant<T, E> mVariant;
};

template <typename E>
class unexpected {
 public:
  constexpr unexpected(const unexpected&) = default;

  template <typename T>
  constexpr explicit unexpected(T&& e) : mError(std::forward<T>(e)) {}

  template <class... Args,
            ENABLE_IF(std::is_constructible<E, Args&&...>::value)>
  constexpr explicit unexpected(std::in_place_t, Args&&... args)
      : mError(std::forward<Args>(args)...) {}

  constexpr const E& value() const& noexcept { return mError; }
  constexpr E& value() & noexcept { return mError; }
  constexpr const E&& value() const&& noexcept { return std::move(mError); }
  constexpr E&& value() && noexcept { return std::move(mError); }

 private:
  E mError;
};

#define GFXSTREAM_EXPECT(x)                                 \
  ({                                                        \
    auto local_expected = (x);                              \
    if (!local_expected.ok()) {                             \
      return gfxstream::unexpected(local_expected.error()); \
    };                                                      \
    std::move(local_expected.value());                      \
  })

class Ok {};

}  // namespace gfxstream