//
// Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/unittests/selector/id_allocator_test_helper.h"

#include <memory>
#include <unordered_set>

#include "common/libs/utils/contains.h"
#include "host/commands/cvd/selector/unique_resource_allocator.h"

namespace cuttlefish::selector {

TEST_P(OneEachTest, GetAnyAvailableOne) {
  const auto resources = GetParam();
  auto allocator = UniqueResourceAllocator<unsigned>::New(resources);
  std::unordered_set<unsigned> expected_ids{resources.cbegin(),
                                            resources.cend()};

  for (int i = 0; i < resources.size(); i++) {
    auto id_opt = allocator.UniqueItem();
    ASSERT_TRUE(id_opt);
    ASSERT_TRUE(Contains(expected_ids, *id_opt));
  }
  ASSERT_FALSE(allocator.UniqueItem());
}

INSTANTIATE_TEST_SUITE_P(
    CvdIdAllocator, OneEachTest,
    testing::Values(std::vector<unsigned>{}, std::vector<unsigned>{1},
                    std::vector<unsigned>{1, 22, 3, 43, 5}));

TEST_F(CvdIdAllocatorTest, ClaimAll) {
  std::vector<unsigned> inputs{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);

  // request inputs.size() items
  ASSERT_TRUE(allocator.UniqueItems(inputs.size()));
  // did it claim all?
  ASSERT_FALSE(allocator.UniqueItem());
}

TEST_F(CvdIdAllocatorTest, StrideBeyond1) {
  std::vector<unsigned> inputs{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);

  ASSERT_TRUE(allocator.UniqueItems(3));
  ASSERT_TRUE(allocator.UniqueItems(4));
  ASSERT_FALSE(allocator.UniqueItems(5));
  ASSERT_TRUE(allocator.UniqueItems(2));
  ASSERT_FALSE(allocator.UniqueItems(2));
}

TEST_F(CvdIdAllocatorTest, Consecutive) {
  std::vector<unsigned> inputs{1, 2, 4, 5, 6, 7, 9, 10, 11};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);

  auto four_consecutive = allocator.UniqueConsecutiveItems(4);
  auto three_consecutive = allocator.UniqueConsecutiveItems(3);
  auto another_three_consecutive = allocator.UniqueConsecutiveItems(3);
  auto two_consecutive = allocator.UniqueConsecutiveItems(2);

  ASSERT_TRUE(four_consecutive);
  ASSERT_TRUE(three_consecutive);
  ASSERT_FALSE(another_three_consecutive);
  ASSERT_TRUE(two_consecutive);
  // it's empty
  ASSERT_FALSE(allocator.UniqueItem()) << "one or more left";
}

TEST_F(CvdIdAllocatorTest, Take) {
  std::vector<unsigned> inputs{4, 5, 9};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);

  auto four = allocator.Take(4);
  auto nine = allocator.Take(9);
  // wrong
  auto twenty = allocator.Take(20);

  ASSERT_TRUE(four);
  ASSERT_TRUE(nine);
  ASSERT_FALSE(twenty);
}

TEST_F(CvdIdAllocatorTest, TakeAll) {
  std::vector<unsigned> inputs{4, 5, 9, 10};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);

  ASSERT_FALSE(allocator.TakeAll<std::vector<unsigned>>({4, 5, 11}));
  ASSERT_TRUE(allocator.TakeAll<std::vector<unsigned>>({4, 5, 10}));
  ASSERT_FALSE(allocator.TakeAll<std::vector<unsigned>>({9, 10}));
  ASSERT_TRUE(allocator.TakeAll<std::vector<unsigned>>({9}));
}

TEST_F(CvdIdAllocatorTest, TakeRange) {
  std::vector<unsigned> inputs{1, 2, 4, 5, 6, 7, 8, 9, 10, 11};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);

  ASSERT_TRUE(allocator.TakeRange(5, 12));
  ASSERT_FALSE(allocator.TakeRange(2, 4));
}

TEST_F(CvdIdAllocatorTest, ReclaimAll) {
  std::vector<unsigned> inputs{1, 2, 4, 5, 6, 7, 8, 9, 10, 11};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);
  if (!allocator.TakeAll(inputs)) {
    GTEST_SKIP() << "In set up for Reclaim, TakeAll failed.";
  }

  // as 3 is not in inputs/allocator, this will fail, and
  // allocator won't be updated
  ASSERT_FALSE(allocator.ReclaimAll(std::vector<unsigned>{2, 3, 4, 5}));
  ASSERT_TRUE(allocator.ReclaimAll(inputs));
  // no effect but returns true
  ASSERT_TRUE(allocator.ReclaimAll(inputs));
  // see if actually the resources/ids are returned to the allocator
  ASSERT_TRUE(allocator.TakeAll(inputs));
}

TEST_F(CvdIdAllocatorTest, ReclaimEmptyPool) {
  std::vector<unsigned> empty_pool;
  auto allocator = UniqueResourceAllocator<unsigned>::New(empty_pool);

  ASSERT_FALSE(allocator.Reclaim(3));
  ASSERT_FALSE(allocator.ReclaimAll(std::vector<unsigned>{1, 2}));
  ASSERT_TRUE(allocator.ReclaimAll(std::vector<unsigned>{}));
}

TEST_P(ReclaimTest, Reclaim) {
  const auto inputs{GetParam()};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);
  if (!allocator.TakeAll(inputs)) {
    GTEST_SKIP() << "In set up for Reclaim, TakeAll failed.";
  }

  ASSERT_TRUE(allocator.Reclaim(7));
  ASSERT_FALSE(allocator.Reclaim(100));
  ASSERT_TRUE(allocator.Take(7));
}

INSTANTIATE_TEST_SUITE_P(
    CvdIdAllocator, ReclaimTest,
    testing::Values(std::vector<unsigned>{7}, std::vector<unsigned>{7, 3},
                    std::vector<unsigned>{1, 22, 3, 43, 7}));

}  // namespace cuttlefish::selector
