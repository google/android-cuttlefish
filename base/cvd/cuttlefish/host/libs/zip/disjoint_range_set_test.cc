//
// Copyright (C) 2023 The Android Open Source Project
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

#include <optional>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace cuttlefish {
namespace {

TEST(DisjointRangeSet, NothingInEmptySet) {
  DisjointRangeSet set;

  EXPECT_FALSE(set.ContainsRange(1, 10));
  EXPECT_FALSE(set.ContainsRange(0, 1));
}

TEST(DisjointRangeSet, SingleMember) {
  DisjointRangeSet set;

  set.InsertRange(5, 10);

  EXPECT_FALSE(set.ContainsRange(4, 5));
  EXPECT_FALSE(set.ContainsRange(4, 6));
  EXPECT_TRUE(set.ContainsRange(5, 6));
  EXPECT_TRUE(set.ContainsRange(7, 9));
  EXPECT_TRUE(set.ContainsRange(5, 10));
  EXPECT_TRUE(set.ContainsRange(9, 10));
  EXPECT_FALSE(set.ContainsRange(9, 11));

  EXPECT_EQ(set.EndOfContainingRange(4), std::nullopt);
  EXPECT_EQ(set.EndOfContainingRange(5), std::make_optional(10));
  EXPECT_EQ(set.EndOfContainingRange(6), std::make_optional(10));
  EXPECT_EQ(set.EndOfContainingRange(9), std::make_optional(10));
  EXPECT_EQ(set.EndOfContainingRange(10), std::nullopt);
  EXPECT_EQ(set.EndOfContainingRange(11), std::nullopt);

  std::vector<std::pair<uint64_t, uint64_t>> expected = {{5, 10}};
  EXPECT_EQ(set.AllRanges(), expected);
}

TEST(DisjointRangeSet, DisjointMembers) {
  DisjointRangeSet set;

  set.InsertRange(5, 10);
  set.InsertRange(15, 20);

  EXPECT_FALSE(set.ContainsRange(4, 5));
  EXPECT_FALSE(set.ContainsRange(4, 6));

  EXPECT_TRUE(set.ContainsRange(5, 6));
  EXPECT_TRUE(set.ContainsRange(7, 9));

  EXPECT_FALSE(set.ContainsRange(9, 11));
  EXPECT_FALSE(set.ContainsRange(12, 14));
  EXPECT_FALSE(set.ContainsRange(14, 16));

  EXPECT_TRUE(set.ContainsRange(16, 18));

  EXPECT_FALSE(set.ContainsRange(18, 22));

  EXPECT_FALSE(set.ContainsRange(7, 17));

  EXPECT_EQ(set.EndOfContainingRange(4), std::nullopt);
  EXPECT_EQ(set.EndOfContainingRange(5), std::make_optional(10));
  EXPECT_EQ(set.EndOfContainingRange(6), std::make_optional(10));
  EXPECT_EQ(set.EndOfContainingRange(9), std::make_optional(10));
  EXPECT_EQ(set.EndOfContainingRange(10), std::nullopt);
  EXPECT_EQ(set.EndOfContainingRange(11), std::nullopt);

  EXPECT_EQ(set.EndOfContainingRange(14), std::nullopt);
  EXPECT_EQ(set.EndOfContainingRange(15), std::make_optional(20));
  EXPECT_EQ(set.EndOfContainingRange(16), std::make_optional(20));
  EXPECT_EQ(set.EndOfContainingRange(19), std::make_optional(20));
  EXPECT_EQ(set.EndOfContainingRange(20), std::nullopt);
  EXPECT_EQ(set.EndOfContainingRange(21), std::nullopt);

  std::vector<std::pair<uint64_t, uint64_t>> expected = {{5, 10}, {15, 20}};
  EXPECT_EQ(set.AllRanges(), expected);
}

TEST(DisjointRangeSet, MergingOverlappingRanges) {
  DisjointRangeSet set;

  set.InsertRange(5, 10);
  set.InsertRange(15, 20);
  set.InsertRange(7, 17);

  EXPECT_FALSE(set.ContainsRange(4, 5));
  EXPECT_FALSE(set.ContainsRange(4, 6));

  EXPECT_TRUE(set.ContainsRange(5, 6));
  EXPECT_TRUE(set.ContainsRange(7, 9));
  EXPECT_TRUE(set.ContainsRange(9, 11));
  EXPECT_TRUE(set.ContainsRange(12, 14));
  EXPECT_TRUE(set.ContainsRange(14, 16));
  EXPECT_TRUE(set.ContainsRange(16, 18));
  EXPECT_TRUE(set.ContainsRange(7, 17));

  EXPECT_FALSE(set.ContainsRange(18, 22));

  std::vector<std::pair<uint64_t, uint64_t>> expected = {{5, 20}};
  EXPECT_EQ(set.AllRanges(), expected);
}

TEST(DisjointRangeSet, MergingAdjacentRanges) {
  DisjointRangeSet set;

  set.InsertRange(5, 10);
  set.InsertRange(10, 15);

  EXPECT_FALSE(set.ContainsRange(4, 5));
  EXPECT_FALSE(set.ContainsRange(4, 6));
  EXPECT_TRUE(set.ContainsRange(5, 6));
  EXPECT_TRUE(set.ContainsRange(7, 9));
  EXPECT_TRUE(set.ContainsRange(9, 11));
  EXPECT_TRUE(set.ContainsRange(12, 14));
  EXPECT_FALSE(set.ContainsRange(14, 16));

  std::vector<std::pair<uint64_t, uint64_t>> expected = {{5, 15}};
  EXPECT_EQ(set.AllRanges(), expected);
}

TEST(DisjointRangeSet, MergingSameStart) {
  DisjointRangeSet set;

  set.InsertRange(5, 10);
  set.InsertRange(5, 15);

  EXPECT_FALSE(set.ContainsRange(4, 5));
  EXPECT_FALSE(set.ContainsRange(4, 6));

  EXPECT_TRUE(set.ContainsRange(5, 6));
  EXPECT_TRUE(set.ContainsRange(7, 9));
  EXPECT_TRUE(set.ContainsRange(9, 11));
  EXPECT_TRUE(set.ContainsRange(12, 14));

  EXPECT_FALSE(set.ContainsRange(15, 16));
  EXPECT_FALSE(set.ContainsRange(14, 16));

  std::vector<std::pair<uint64_t, uint64_t>> expected = {{5, 15}};
  EXPECT_EQ(set.AllRanges(), expected);
}

TEST(DisjointRangeSet, MergingSameEnd) {
  DisjointRangeSet set;

  set.InsertRange(10, 15);
  set.InsertRange(5, 15);

  EXPECT_FALSE(set.ContainsRange(4, 5));
  EXPECT_FALSE(set.ContainsRange(4, 6));

  EXPECT_TRUE(set.ContainsRange(5, 6));
  EXPECT_TRUE(set.ContainsRange(7, 9));
  EXPECT_TRUE(set.ContainsRange(9, 11));
  EXPECT_TRUE(set.ContainsRange(12, 14));

  EXPECT_FALSE(set.ContainsRange(15, 16));
  EXPECT_FALSE(set.ContainsRange(14, 16));

  std::vector<std::pair<uint64_t, uint64_t>> expected = {{5, 15}};
  EXPECT_EQ(set.AllRanges(), expected);
}

}  // namespace
}  // namespace cuttlefish
