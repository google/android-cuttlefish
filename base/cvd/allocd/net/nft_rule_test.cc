/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "allocd/net/nft_rule.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <utility>

#include "allocd/test/mock_nftables.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::Eq;
using ::testing::Return;

TEST(NftRuleTest, CreateAndAutoDeleteOnDestruction) {
  MockNftables mock_nft;
  constexpr uint32_t kHandle = 42;

  EXPECT_CALL(mock_nft, AddRule("ip", "table1", "chain1", "content1"))
      .WillOnce(Return(kHandle));
  EXPECT_CALL(mock_nft, DeleteRule("ip", "table1", "chain1", kHandle))
      .WillOnce(Return(Result<void>{}));

  {
    auto rule = NftRule::Create(mock_nft, "ip", "table1", "chain1", "content1");
    EXPECT_THAT(rule, IsOk());
  }
}

TEST(NftRuleTest, MoveConstructorTransfersOwnership) {
  MockNftables mock_nft;
  constexpr uint32_t kHandle = 100;

  EXPECT_CALL(mock_nft, AddRule("ip", "table1", "chain1", "content1"))
      .WillOnce(Return(kHandle));
  EXPECT_CALL(mock_nft, DeleteRule("ip", "table1", "chain1", kHandle))
      .WillOnce(Return(Result<void>{}));

  {
    auto rule1 = NftRule::Create(mock_nft, "ip", "table1", "chain1", "content1");
    ASSERT_THAT(rule1, IsOk());

    NftRule rule2(std::move(*rule1));
    // When rule1 leaves scope, it should not call DeleteRule.
    // Only rule2 leaving scope will call DeleteRule once.
  }
}

}  // namespace
}  // namespace cuttlefish
