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
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "common/libs/utils/contains.h"

namespace cuttlefish {
namespace selector {

template <typename T>
class UniqueResourceAllocator {
 public:
  /*
   * Creates the singleton object.
   *
   * Call this function once during the entire program's life
   */
  static UniqueResourceAllocator& Create(const std::vector<T>& pool) {
    static UniqueResourceAllocator singleton_allocator(pool);
    return singleton_allocator;
  }

  /*
   * use this when a new object for relatively limited scope is required.
   */
  static UniqueResourceAllocator New(const std::vector<T>& pool) {
    return UniqueResourceAllocator(pool);
  }

  std::optional<T> UniqueItem();
  // gives n unique integers from the pool, and then remove them from the pool
  std::optional<std::unordered_set<T>> UniqueItems(const int n);

  template <typename V = T>
  std::enable_if_t<std::is_integral<V>::value,
                   std::optional<std::unordered_set<T>>>
  UniqueConsecutiveItems(const int n);

  template <typename Container>
  bool ReclaimAll(const Container& items);
  bool Reclaim(const T& t);

  // takes t if available
  // returns false if not available or not in the pool at all
  bool Take(const T& t);

  template <typename Container>
  bool TakeAll(const Container& ts);

  /*
   * If the range is available, returns the resources from the pool
   *
   * Otherwise, makes no change in the internal data structure but
   * returns false.
   */
  template <typename V = T>
  std::enable_if_t<std::is_integral<V>::value, bool> TakeRange(
      const T& start_inclusive, const T& end_exclusive);

 private:
  template <typename Container>
  UniqueResourceAllocator(const Container& items)
      : available_resources_{items.cbegin(), items.cend()} {}

  /*
   * If the range is available, returns the resources from the pool
   *
   * Otherwise, makes no change in the internal data structure but
   * returns false.
   */
  template <typename V = T>
  std::enable_if_t<std::is_integral<V>::value, bool> TakeRangeInternal(
      const T& start_inclusive, const T& end_exclusive);

  std::unordered_set<T> available_resources_;
  std::unordered_set<T> allocated_resources_;
  std::mutex mutex_;
};

template <typename T>
std::optional<T> UniqueResourceAllocator<T>::UniqueItem() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (available_resources_.empty()) {
    return std::nullopt;
  }
  const auto i = *available_resources_.cbegin();
  available_resources_.erase(i);
  allocated_resources_.insert(i);
  return {i};
}

template <typename T>
std::optional<std::unordered_set<T>> UniqueResourceAllocator<T>::UniqueItems(
    const int n) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (n <= 0 || available_resources_.size() < n) {
    return std::nullopt;
  }
  std::unordered_set<T> result;
  for (int i = 0; i < n; i++) {
    const auto elem = *available_resources_.cbegin();
    available_resources_.erase(elem);
    result.insert(elem);
    allocated_resources_.insert(elem);
  }
  return {result};
}

template <typename T>
template <typename V>
std::enable_if_t<std::is_integral<V>::value,
                 std::optional<std::unordered_set<T>>>
UniqueResourceAllocator<T>::UniqueConsecutiveItems(const int n) {
  static_assert(std::is_same<T, V>::value);
  std::lock_guard<std::mutex> lock(mutex_);
  if (n <= 0 || available_resources_.size() < n) {
    return std::nullopt;
  }

  for (const auto& available_resource : available_resources_) {
    auto start_inclusive = available_resource;
    if (!TakeRangeInternal(start_inclusive, start_inclusive + n)) {
      continue;
    }
    std::vector<T> range(n);
    std::iota(range.begin(), range.end(), start_inclusive);
    return {std::unordered_set<T>(range.cbegin(), range.cend())};
  }
  return std::nullopt;
}

template <typename T>
template <typename Container>
bool UniqueResourceAllocator<T>::ReclaimAll(const Container& items) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& i : items) {
    if (!Contains(allocated_resources_, i) &&
        !Contains(available_resources_, i)) {
      return false;
    }
    allocated_resources_.erase(i);
    available_resources_.insert(i);
  }
  return true;
}

template <typename T>
bool UniqueResourceAllocator<T>::Reclaim(const T& t) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!Contains(allocated_resources_, t) &&
      !Contains(available_resources_, t)) {
    return false;
  }
  allocated_resources_.erase(t);
  available_resources_.insert(t);
  return true;
}

template <typename T>
bool UniqueResourceAllocator<T>::Take(const T& t) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!Contains(available_resources_, t)) {
    return false;
  }
  available_resources_.erase(t);
  allocated_resources_.insert(t);
  return true;
}

template <typename T>
template <typename Container>
bool UniqueResourceAllocator<T>::TakeAll(const Container& ts) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& t : ts) {
    if (!Contains(available_resources_, t)) {
      return false;
    }
  }
  for (const auto& t : ts) {
    available_resources_.erase(t);
    allocated_resources_.insert(t);
  }
  return true;
}

template <typename T>
template <typename V>
std::enable_if_t<std::is_integral<V>::value, bool>
UniqueResourceAllocator<T>::TakeRange(const T& start_inclusive,
                                      const T& end_exclusive) {
  static_assert(std::is_same<T, V>::value);
  std::lock_guard<std::mutex> lock(mutex_);
  return TakeRangeInternal(start_inclusive, end_exclusive);
}

template <typename T>
template <typename V>
std::enable_if_t<std::is_integral<V>::value, bool>
UniqueResourceAllocator<T>::TakeRangeInternal(const T& start_inclusive,
                                              const T& end_exclusive) {
  static_assert(std::is_same<T, V>::value);
  for (auto cursor = start_inclusive; cursor < end_exclusive; cursor++) {
    if (!Contains(available_resources_, cursor)) {
      return false;
    }
  }
  for (auto cursor = start_inclusive; cursor < end_exclusive; cursor++) {
    available_resources_.erase(cursor);
    allocated_resources_.insert(cursor);
  }
  return true;
}

}  // namespace selector
}  // namespace cuttlefish
