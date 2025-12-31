//
// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "cuttlefish/host/libs/zip/disjoint_range_set.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "absl/log/check.h"

namespace cuttlefish {
namespace {

class Range {
 public:
  Range(uint64_t start, uint64_t end) : start_(start), end_(end) {}

  bool Contains(const Range& other) const {
    return start_ <= other.start_ && end_ >= other.end_;
  }

  bool CanMerge(const Range& other) const {
    return start_ <= other.end_ && other.start_ <= end_;
  }

  // Preconditions: `CanMerge(other)`
  Range Merge(const Range& other) const {
    return Range(std::min(start_, other.start_), std::max(end_, other.end_));
  }

  bool operator<(const Range& other) const { return start_ < other.start_; }
  bool operator==(const Range& other) const {
    return start_ == other.start_ && end_ == other.end_;
  }

  std::pair<uint64_t, uint64_t> AsPair() const {
    return std::make_pair(start_, end_);
  }

  uint64_t End() const { return end_; }

 private:
  uint64_t start_;
  uint64_t end_;
};

using Iterator = std::set<Range>::iterator;

}  // namespace

struct DisjointRangeSet::Impl {
  std::set<Range> ranges_;
};

DisjointRangeSet::DisjointRangeSet() : impl_(new Impl) {}

DisjointRangeSet::DisjointRangeSet(const DisjointRangeSet& other)
    : impl_(new Impl) {
  impl_->ranges_ = other.impl_->ranges_;
}

DisjointRangeSet::DisjointRangeSet(DisjointRangeSet&& other) : impl_(new Impl) {
  std::swap(impl_, other.impl_);
}

DisjointRangeSet& DisjointRangeSet::operator=(const DisjointRangeSet& other) {
  impl_->ranges_ = other.impl_->ranges_;
  return *this;
}

DisjointRangeSet& DisjointRangeSet::operator=(DisjointRangeSet&& other) {
  impl_ = std::move(other.impl_);
  other.impl_.reset(new Impl);
  return *this;
}

DisjointRangeSet::~DisjointRangeSet() = default;

bool DisjointRangeSet::ContainsRange(uint64_t start, uint64_t end) const {
  CHECK_LE(start, end) << "Invalid range: expected start <= end";

  const std::set<Range>& ranges = impl_->ranges_;

  if (ranges.empty()) {
    return false;
  }
  Range range(start, end);

  Iterator range_it = ranges.upper_bound(range);
  if (range_it != ranges.begin()) {
    range_it--;
  }
  return range_it->Contains(range);
}

void DisjointRangeSet::InsertRange(uint64_t start, uint64_t end) {
  CHECK_LE(start, end) << "Invalid range: expected start <= end";

  if (ContainsRange(start, end)) {
    return;
  }

  std::set<Range>& ranges = impl_->ranges_;
  Range new_range(start, end);

  // Merge in ranges that start after the newly inserted range starts, if the
  // newly inserted range would overlap with them. `it` always points to the
  // range with the next highest starting location.
  Iterator it = ranges.upper_bound(new_range);
  while (it != ranges.end() && new_range.CanMerge(*it)) {
    new_range = new_range.Merge(*it);
    it = ranges.erase(it);
  }
  // Merge a range that start before the newly inserted range, if the newly
  // inserted range overlaps with the previous one. `it` initially points at the
  // range with the next highest starting location, and `--it` moves it to point
  // to the element directly before that. This works differently than the
  // forwards merge because while `it` can safely be the `end()` sentinel past
  // the end of the collection, there is no equivalent before-the-start sentinel
  // value. There can be at most one range before that overlaps with this one.
  if (it != ranges.begin() && new_range.CanMerge(*(--it))) {
    new_range = new_range.Merge(*it);
    it = ranges.erase(it);
  }
  ranges.emplace(new_range);
}

std::optional<uint64_t> DisjointRangeSet::EndOfContainingRange(
    uint64_t start) const {
  const std::set<Range>& ranges = impl_->ranges_;

  Range test(start, start + 1);

  Iterator it = ranges.upper_bound(test);
  if (it == ranges.begin()) {
    return std::nullopt;
  }
  it--;
  return it->Contains(test) ? std::make_optional(it->End()) : std::nullopt;
}

std::vector<std::pair<uint64_t, uint64_t>> DisjointRangeSet::AllRanges() const {
  std::vector<std::pair<uint64_t, uint64_t>> pairs;
  pairs.reserve(impl_->ranges_.size());
  for (const Range& range : impl_->ranges_) {
    pairs.emplace_back(range.AsPair());
  }
  return pairs;
}

bool DisjointRangeSet::operator==(const DisjointRangeSet& other) const {
  return impl_->ranges_ == other.impl_->ranges_;
}

}  // namespace cuttlefish
