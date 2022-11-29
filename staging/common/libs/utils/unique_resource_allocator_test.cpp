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

#include <unordered_set>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/unique_resource_allocator.h"
#include "common/libs/utils/unique_resource_allocator_test.h"

namespace cuttlefish {

TEST_P(OneEachTest, GetAnyAvailableOne) {
  const auto resources = GetParam();
  auto allocator = UniqueResourceAllocator<unsigned>::New(resources);
  if (!allocator) {
    GTEST_SKIP() << "Memory allocation failed but we aren't testing it.";
  }
  std::unordered_set<unsigned> expected_ids{resources.cbegin(),
                                            resources.cend()};
  using Reservation = UniqueResourceAllocator<unsigned>::Reservation;

  std::vector<Reservation> allocated;
  for (int i = 0; i < resources.size(); i++) {
    auto id_opt = allocator->UniqueItem();
    ASSERT_TRUE(id_opt);
    ASSERT_TRUE(Contains(expected_ids, id_opt->Get()));
    allocated.emplace_back(std::move(*id_opt));
  }
  ASSERT_FALSE(allocator->UniqueItem());
}

INSTANTIATE_TEST_SUITE_P(
    CvdIdAllocator, OneEachTest,
    testing::Values(std::vector<unsigned>{}, std::vector<unsigned>{1},
                    std::vector<unsigned>{1, 22, 3, 43, 5}));

TEST_F(CvdIdAllocatorTest, ClaimAll) {
  std::vector<unsigned> inputs{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);
  if (!allocator) {
    GTEST_SKIP() << "Memory allocation failed but we aren't testing it.";
  }

  // request inputs.size() items
  auto allocated_items_opt = allocator->UniqueItems(inputs.size());
  ASSERT_TRUE(allocated_items_opt);
  ASSERT_EQ(allocated_items_opt->size(), inputs.size());
  // did it claim all?
  ASSERT_FALSE(allocator->UniqueItem());
}

TEST_F(CvdIdAllocatorTest, StrideBeyond) {
  std::vector<unsigned> inputs{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);
  if (!allocator) {
    GTEST_SKIP() << "Memory allocation failed but we aren't testing it.";
  }

  auto three_opt = allocator->UniqueItems(3);
  auto four_opt = allocator->UniqueItems(4);
  auto five_opt = allocator->UniqueItems(5);
  auto two_opt = allocator->UniqueItems(2);
  auto another_two_opt = allocator->UniqueItems(2);

  ASSERT_TRUE(three_opt);
  ASSERT_TRUE(four_opt);
  ASSERT_FALSE(five_opt);
  ASSERT_TRUE(two_opt);
  ASSERT_FALSE(another_two_opt);
}

TEST_F(CvdIdAllocatorTest, Consecutive) {
  std::vector<unsigned> inputs{1, 2, 4, 5, 6, 7, 9, 10, 11};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);
  if (!allocator) {
    GTEST_SKIP() << "Memory allocation failed but we aren't testing it.";
  }

  auto four_consecutive = allocator->UniqueConsecutiveItems(4);
  auto three_consecutive = allocator->UniqueConsecutiveItems(3);
  auto another_three_consecutive = allocator->UniqueConsecutiveItems(3);
  auto two_consecutive = allocator->UniqueConsecutiveItems(2);

  ASSERT_TRUE(four_consecutive);
  ASSERT_TRUE(three_consecutive);
  ASSERT_FALSE(another_three_consecutive);
  ASSERT_TRUE(two_consecutive);
  // it's empty
  ASSERT_FALSE(allocator->UniqueItem()) << "one or more left";
}

TEST_F(CvdIdAllocatorTest, Take) {
  std::vector<unsigned> inputs{4, 5, 9};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);
  if (!allocator) {
    GTEST_SKIP() << "Memory allocation failed but we aren't testing it.";
  }

  auto four = allocator->Take(4);
  auto nine = allocator->Take(9);
  // wrong
  auto twenty = allocator->Take(20);

  ASSERT_TRUE(four);
  ASSERT_TRUE(nine);
  ASSERT_FALSE(twenty);
}

TEST_F(CvdIdAllocatorTest, TakeAll) {
  std::vector<unsigned> inputs{4, 5, 9, 10};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);
  if (!allocator) {
    GTEST_SKIP() << "Memory allocation failed but we aren't testing it.";
  }

  auto take_4_5_11 = allocator->TakeAll<std::vector<unsigned>>({4, 5, 11});
  auto take_4_5_10 = allocator->TakeAll<std::vector<unsigned>>({4, 5, 10});
  auto take_9_10 = allocator->TakeAll<std::vector<unsigned>>({9, 10});
  auto take_9 = allocator->TakeAll<std::vector<unsigned>>({9});

  ASSERT_FALSE(take_4_5_11);
  ASSERT_TRUE(take_4_5_10);
  ASSERT_FALSE(take_9_10);
  ASSERT_TRUE(take_9);
}

TEST_F(CvdIdAllocatorTest, TakeRange) {
  std::vector<unsigned> inputs{1, 2, 4, 5, 6, 7, 8, 9, 10, 11};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);
  if (!allocator) {
    GTEST_SKIP() << "Memory allocation failed but we aren't testing it.";
  }

  auto take_range_5_12 = allocator->TakeRange(5, 12);
  // shall fail as 3 is missing
  auto take_range_2_4 = allocator->TakeRange(2, 4);

  ASSERT_TRUE(take_range_5_12);
  ASSERT_FALSE(take_range_2_4);
}

TEST_F(CvdIdAllocatorTest, Reclaim) {
  std::vector<unsigned> inputs{1, 2, 4, 5, 6, 7, 8, 9, 10, 11};
  auto allocator = UniqueResourceAllocator<unsigned>::New(inputs);
  if (!allocator) {
    GTEST_SKIP() << "Memory allocation failed but we aren't testing it.";
  }
  {
    auto take_range_5_12 = allocator->TakeRange(5, 12);

    ASSERT_TRUE(take_range_5_12);
    ASSERT_FALSE(allocator->TakeRange(5, 12));
  }
  // take_range_5_12 went out of scope, so resources were reclaimed
  ASSERT_TRUE(allocator->TakeRange(5, 12));
}

}  // namespace cuttlefish
