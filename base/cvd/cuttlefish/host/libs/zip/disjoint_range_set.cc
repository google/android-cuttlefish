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

namespace cuttlefish {
namespace {

class Range {
 public:
  Range(uint64_t start, uint64_t end) : start_(start), end_(end) {}

  bool Contains(uint64_t pnt) const { return start_ <= pnt && pnt < end_; }
  bool Contains(const Range& other) const {
    return start_ <= other.start_ && end_ >= other.end_;
  }

  std::optional<Range> MergeIfPossible(const Range& other) const {
    bool intersects = Contains(other.start_) || Contains(other.end_) ||
                      other.Contains(start_) || other.Contains(end_);
    bool adjacent = start_ == other.end_ || end_ == other.start_;
    if (!intersects && !adjacent) {
      return std::nullopt;
    }
    Range new_range(std::min(start_, other.start_), std::max(end_, other.end_));
    return new_range;
  }

  bool operator<(const Range& other) const { return start_ < other.start_; }
  bool operator==(const Range& other) const {
    return start_ == other.start_ && end_ == other.end_;
  }

  std::pair<uint64_t, uint64_t> AsPair() const {
    return std::make_pair(start_, end_);
  }

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
  impl_.reset(new Impl);
  std::swap(impl_, other.impl_);
  return *this;
}

DisjointRangeSet::~DisjointRangeSet() = default;

bool DisjointRangeSet::ContainsRange(uint64_t start, uint64_t end) const {
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
  if (ContainsRange(start, end)) {
    return;
  }

  std::set<Range>& ranges = impl_->ranges_;
  Range new_range(start, end);

  Iterator it = ranges.emplace(new_range).first;
  if (std::optional<Range> merge = it->MergeIfPossible(new_range); merge) {
    ranges.erase(it);
    it = ranges.emplace(new_range).first;
  }

  while (it != ranges.begin()) {
    it--;
    if (std::optional<Range> merge = it->MergeIfPossible(new_range); merge) {
      ranges.erase(it);
      it = ranges.emplace(new_range = *merge).first;
    } else {
      it++;
      break;
    }
  }

  it++;
  while (it != ranges.end()) {
    if (std::optional<Range> merge = it->MergeIfPossible(new_range); merge) {
      it--;
      it = ranges.erase(it);
      ranges.erase(it);
      it = ranges.emplace(new_range = *merge).first;
      it++;
    } else {
      break;
    }
  }
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
