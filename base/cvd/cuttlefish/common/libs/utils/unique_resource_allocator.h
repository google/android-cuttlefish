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
#include <optional>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/utils/contains.h"

namespace cuttlefish {

/**
 * Generic allocator that can provide RAII-aware resource reservations.
 *
 * See go/cf-resource-allocator-utils for more details.
 */
template <typename T>
class UniqueResourceAllocator {
  template <typename U>
  using RemoveCvref =
      typename std::remove_cv_t<typename std::remove_reference_t<U>>;

 public:
  /*
   * Returning the inner resource to the pool at destruction time
   *
   * The pool must live longer than the resources. Use this like you use
   * std::unique_ptr.
   */
  class Reservation {
    friend class UniqueResourceAllocator;
    friend class ReservationSet;

   public:
    Reservation(const Reservation&) = delete;
    Reservation(Reservation&& src)
        : resource_pool_(src.resource_pool_), resource_(src.resource_) {
      src.resource_pool_ = nullptr;
    }
    Reservation& operator=(const Reservation&) = delete;
    Reservation& operator=(Reservation&& src) = delete;

    bool operator==(const Reservation& src) const {
      return (resource_ == src.resource_ &&
              resource_pool_ == src.resource_pool_);
    }

    ~Reservation() {
      if (resource_pool_) {
        resource_pool_->Reclaim(*resource_);
      }
    }
    const T& Get() const { return *resource_; }

   private:
    Reservation(UniqueResourceAllocator& resource_pool, const T& resource)
        : resource_pool_(std::addressof(resource_pool)),
          resource_(std::addressof(resource)) {}
    /*
     * Once this Reservation is std::move-ed out to other object,
     * resource_pool_ should be invalidated, and resource_ shouldn't
     * be tried to be returned to the invalid resource_pool_
     */
    UniqueResourceAllocator* resource_pool_;
    const T* resource_;
  };

  struct ReservationHash {
    std::size_t operator()(const Reservation& resource_wrapper) const {
      return std::hash<const T*>()(std::addressof(resource_wrapper.Get()));
    }
  };
  using ReservationSet = std::unordered_set<Reservation, ReservationHash>;
  /*
   * Creates the singleton object.
   *
   * Call this function once during the entire program's life
   */
  static UniqueResourceAllocator& Create(const std::vector<T>& pool) {
    static UniqueResourceAllocator singleton_allocator(pool);
    return singleton_allocator;
  }

  static std::unique_ptr<UniqueResourceAllocator> New(
      const std::vector<T>& pool) {
    UniqueResourceAllocator* new_allocator = new UniqueResourceAllocator(pool);
    return std::unique_ptr<UniqueResourceAllocator>(new_allocator);
  }

  // Adds the elements from new pool that did not belong to and have not
  // belonged to the current pool of the allocator. returns the leftover
  std::vector<T> ExpandPool(std::vector<T> another_pool) {
    std::lock_guard lock(mutex_);
    std::vector<T> not_selected;
    for (auto& new_item : another_pool) {
      if (Contains(available_resources_, new_item) ||
          Contains(allocated_resources_, new_item)) {
        not_selected.emplace_back(std::move(new_item));
        continue;
      }
      available_resources_.insert(std::move(new_item));
    }
    return not_selected;
  }

  std::vector<T> ExpandPool(T&& t) {
    std::vector<T> pool_to_add;
    pool_to_add.emplace_back(std::move(t));
    return ExpandPool(std::move(pool_to_add));
  }

  std::vector<T> ExpandPool(const T& t) {
    std::vector<T> pool_to_add;
    pool_to_add.emplace_back(t);
    return ExpandPool(std::move(pool_to_add));
  }

  std::optional<Reservation> UniqueItem() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto itr = available_resources_.begin();
    if (itr == available_resources_.end()) {
      return std::nullopt;
    }
    Reservation r(*this, *(RemoveFromPool(itr)));
    return {std::move(r)};
  }

  // gives n unique integers from the pool, and then remove them from the pool
  std::optional<ReservationSet> UniqueItems(const int n) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (n <= 0 || available_resources_.size() < n) {
      return std::nullopt;
    }
    ReservationSet result;
    for (int i = 0; i < n; i++) {
      auto itr = available_resources_.begin();
      result.insert(Reservation{*this, *(RemoveFromPool(itr))});
    }
    return {std::move(result)};
  }

  template <typename V = T>
  std::enable_if_t<std::is_integral<V>::value, std::optional<ReservationSet>>
  UniqueConsecutiveItems(const int n) {
    static_assert(std::is_same<T, V>::value);
    std::lock_guard<std::mutex> lock(mutex_);
    if (n <= 0 || available_resources_.size() < n) {
      return std::nullopt;
    }

    for (const auto& available_resource : available_resources_) {
      auto start_inclusive = available_resource;
      auto resources_opt =
          TakeRangeInternal(start_inclusive, start_inclusive + n);
      if (!resources_opt) {
        continue;
      }
      return resources_opt;
    }
    return std::nullopt;
  }

  // takes t if available
  // returns false if not available or not in the pool at all
  std::optional<Reservation> Take(const T& t) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto itr = available_resources_.find(t);
    if (itr == available_resources_.end()) {
      return std::nullopt;
    }
    Reservation resource{*this, *(RemoveFromPool(itr))};
    return resource;
  }

  template <typename Container>
  std::optional<ReservationSet> TakeAll(const Container& ts) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : ts) {
      if (!Contains(available_resources_, t)) {
        return std::nullopt;
      }
    }
    ReservationSet resources;
    for (const auto& t : ts) {
      auto itr = available_resources_.find(t);
      resources.insert(Reservation{*this, *(RemoveFromPool(itr))});
    }
    return resources;
  }

  /*
   * If the range is available, returns the resources from the pool
   *
   * Otherwise, makes no change in the internal data structure but
   * returns false.
   */
  template <typename V = T>
  std::enable_if_t<std::is_integral<V>::value, std::optional<ReservationSet>>
  TakeRange(const T& start_inclusive, const T& end_exclusive) {
    static_assert(std::is_same<T, V>::value);
    std::lock_guard<std::mutex> lock(mutex_);
    return TakeRangeInternal(start_inclusive, end_exclusive);
  }

 private:
  template <typename Container>
  UniqueResourceAllocator(const Container& items)
      : available_resources_{items.cbegin(), items.cend()} {}

  bool operator==(const UniqueResourceAllocator& other) const {
    return std::addressof(*this) == std::addressof(other);
  }

  // only called by the destructor of Reservation
  // harder to use Result as this is called by destructors only
  void Reclaim(const T& t) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto itr = allocated_resources_.find(t);
    if (itr == allocated_resources_.end()) {
      if (!Contains(available_resources_, t)) {
        LOG(ERROR) << "The resource " << t << " does not belong to this pool";
        return;
      }
      // already reclaimed.
      return;
    }
    T tmp = std::move(*itr);
    allocated_resources_.erase(itr);
    available_resources_.insert(std::move(tmp));
  }

  /*
   * If the range is available, returns the resources from the pool
   *
   * Otherwise, makes no change in the internal data structure but
   * returns false.
   */
  template <typename V = T>
  std::enable_if_t<std::is_integral<V>::value, std::optional<ReservationSet>>
  TakeRangeInternal(const T& start_inclusive, const T& end_exclusive) {
    static_assert(std::is_same<T, V>::value);
    for (auto cursor = start_inclusive; cursor < end_exclusive; cursor++) {
      if (!Contains(available_resources_, cursor)) {
        return std::nullopt;
      }
    }
    ReservationSet resources;
    for (auto cursor = start_inclusive; cursor < end_exclusive; cursor++) {
      auto itr = available_resources_.find(cursor);
      resources.insert(Reservation{*this, *(RemoveFromPool(itr))});
    }
    return resources;
  }

  /*
   * Moves *itr from available_resources_ to allocated_resources_, and returns
   * the pointer of the object in the allocated_resources_. The pointer is never
   * nullptr as it is std::addressof(an object in the unordered_set buffer).
   *
   * The itr must belong to available_resources_.
   */
  const T* RemoveFromPool(const typename std::unordered_set<T>::iterator itr) {
    T tmp = std::move(*itr);
    available_resources_.erase(itr);
    const auto [new_itr, _] = allocated_resources_.insert(std::move(tmp));
    return std::addressof(*new_itr);
  }
  std::unordered_set<T> available_resources_;
  std::unordered_set<T> allocated_resources_;
  std::mutex mutex_;
};

}  // namespace cuttlefish
