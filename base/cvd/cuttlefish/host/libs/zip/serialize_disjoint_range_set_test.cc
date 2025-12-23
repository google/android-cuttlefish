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

#include "cuttlefish/host/libs/zip/serialize_disjoint_range_set.h"

#include <string>

#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/host/libs/zip/disjoint_range_set.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

TEST(DisjointRangeSet, SerializeEmptySet) {
  DisjointRangeSet set;

  std::string str = Serialize(set);

  EXPECT_THAT(DeserializeDisjointRangeSet(str), IsOkAndValue(set));
}

TEST(DisjointRangeSet, SerializeOneMember) {
  DisjointRangeSet set;

  set.InsertRange(5, 15);

  std::string str = Serialize(set);

  EXPECT_THAT(DeserializeDisjointRangeSet(str), IsOkAndValue(set));
}

TEST(DisjointRangeSet, SerializeTwoMembers) {
  DisjointRangeSet set;

  set.InsertRange(5, 15);
  set.InsertRange(25, 35);

  std::string str = Serialize(set);

  EXPECT_THAT(DeserializeDisjointRangeSet(str), IsOkAndValue(set));
}

}  // namespace
}  // namespace cuttlefish
