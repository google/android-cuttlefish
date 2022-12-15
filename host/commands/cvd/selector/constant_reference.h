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

#include <functional>

namespace cuttlefish {
namespace selector {

class LocalInstanceGroup;
class LocalInstance;

template <typename T>
class ConstRef {
  static_assert(std::is_same<T, LocalInstanceGroup>::value ||
                std::is_same<T, LocalInstance>::value);

 public:
  ConstRef(ConstRef& ref) = default;
  ConstRef(const ConstRef& ref) = default;
  ConstRef(ConstRef&& ref) = default;

  ConstRef(const T& t) : inner_wrapper_(t) {}
  ConstRef(T&&) = delete;

  ConstRef& operator=(const ConstRef& other) {
    inner_wrapper_ = other.inner_wrapper_;
    return *this;
  }

  operator const T&() const noexcept { return inner_wrapper_.get(); }

  const T& Get() const noexcept { return inner_wrapper_.get(); }

  /**
   * comparison based on the address of underlying object
   *
   * Note that, per instance (group), there is only one LocalInstance(Group)
   * object is created during the program's life time. Besides, they don't
   * offer operator==, either. ConstRef<LocalInstance(Group)> has to be in
   * a set.
   */
  bool operator==(const ConstRef& rhs) const noexcept {
    return std::addressof(Get()) == std::addressof(rhs.Get());
  }

 private:
  std::reference_wrapper<const T> inner_wrapper_;
};

template <class T>
ConstRef<T> Cref(const T& t) noexcept {
  return ConstRef<T>(t);
}

}  // namespace selector
}  // namespace cuttlefish

/**
 * the assumption is, if std::addressof(lhs) != std::addressof(rhs),
 * the two LocalInstance objects are actually different. There is only
 * on LocalInstance(Group) object per a given cuttlefish instance (group).
 */
template <typename T>
struct std::hash<cuttlefish::selector::ConstRef<T>> {
  std::size_t operator()(
      const cuttlefish::selector::ConstRef<T>& ref) const noexcept {
    const auto ptr = std::addressof(ref.Get());
    return std::hash<const T*>()(ptr);
  }
};
