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
#pragma once

#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

namespace cuttlefish {

class DisjointRangeSet {
 public:
  DisjointRangeSet();
  DisjointRangeSet(const DisjointRangeSet&);
  DisjointRangeSet(DisjointRangeSet&&);
  ~DisjointRangeSet();
  DisjointRangeSet& operator=(const DisjointRangeSet&);
  DisjointRangeSet& operator=(DisjointRangeSet&&);

  // Reports if all members from [start,end) are contained.
  bool ContainsRange(uint64_t start, uint64_t end) const;
  // Records that all numbers from [start,end) are contained.
  void InsertRange(uint64_t start, uint64_t end);

  std::vector<std::pair<uint64_t, uint64_t>> AllRanges() const;

  bool operator==(const DisjointRangeSet&) const;

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace cuttlefish
