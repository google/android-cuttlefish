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

#include "allocd/test/fake_nftables.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

class NftRuleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_THAT(fake_.EnsureTable("ip", "table1"), IsOk());
    ASSERT_THAT(fake_.EnsureChain("ip", "table1", "chain1", ""), IsOk());
  }

  FakeNftables fake_;
};

TEST_F(NftRuleTest, CreateAddsRuleWithPrefixedComment) {
  Result<NftRule> rule =
      NftRule::Create(fake_, "ip", "table1", "chain1", "content1", "tag1");
  ASSERT_THAT(rule, IsOk());
  EXPECT_TRUE(
      fake_.HasRuleWithComment("ip", "table1", "chain1", "cvdalloc-tag1"));
}

TEST_F(NftRuleTest, DeletesRuleOnDestruction) {
  {
    Result<NftRule> rule =
        NftRule::Create(fake_, "ip", "table1", "chain1", "content1", "tag1");
    ASSERT_THAT(rule, IsOk());
    EXPECT_TRUE(
        fake_.HasRuleWithComment("ip", "table1", "chain1", "cvdalloc-tag1"));
  }
  EXPECT_FALSE(
      fake_.HasRuleWithComment("ip", "table1", "chain1", "cvdalloc-tag1"));
  EXPECT_EQ(fake_.RuleCount("ip", "table1", "chain1"), 0);
}

TEST_F(NftRuleTest, MoveConstructorTransfersOwnership) {
  {
    Result<NftRule> rule1 =
        NftRule::Create(fake_, "ip", "table1", "chain1", "content1", "tag1");
    ASSERT_THAT(rule1, IsOk());

    NftRule rule2(std::move(*rule1));
    EXPECT_TRUE(
        fake_.HasRuleWithComment("ip", "table1", "chain1", "cvdalloc-tag1"));
  }
  EXPECT_FALSE(
      fake_.HasRuleWithComment("ip", "table1", "chain1", "cvdalloc-tag1"));
  EXPECT_EQ(fake_.RuleCount("ip", "table1", "chain1"), 0);
}

}  // namespace
}  // namespace cuttlefish
