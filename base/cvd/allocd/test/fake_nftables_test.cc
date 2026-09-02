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

#include "allocd/test/fake_nftables.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdint.h>

#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

class FakeNftablesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_THAT(fake_.EnsureTable("ip", "t"), IsOk());
    ASSERT_THAT(fake_.EnsureChain("ip", "t", "c", ""), IsOk());
  }

  FakeNftables fake_;
};

TEST_F(FakeNftablesTest, EnsureTableIsIdempotent) {
  EXPECT_THAT(fake_.EnsureTable("ip", "t"), IsOk());
  EXPECT_TRUE(fake_.HasTable("ip", "t"));
}

TEST_F(FakeNftablesTest, EnsureChainIsIdempotent) {
  EXPECT_THAT(fake_.EnsureChain("ip", "t", "c", ""), IsOk());
  EXPECT_TRUE(fake_.HasChain("ip", "t", "c"));
}

TEST_F(FakeNftablesTest, EnsureChainRequiresTable) {
  EXPECT_THAT(fake_.EnsureChain("ip", "missing", "c", ""), IsError());
}

TEST_F(FakeNftablesTest, AddRuleRequiresTableAndChain) {
  EXPECT_THAT(fake_.AddRule("ip", "missing", "c", "content", "cmt"), IsError());
  EXPECT_THAT(fake_.AddRule("ip", "t", "missing", "content", "cmt"), IsError());
}

TEST_F(FakeNftablesTest, HandlesAreUniqueAndMonotonic) {
  Result<uint64_t> h1 = fake_.AddRule("ip", "t", "c", "content1", "a");
  Result<uint64_t> h2 = fake_.AddRule("ip", "t", "c", "content2", "b");
  ASSERT_THAT(h1, IsOk());
  ASSERT_THAT(h2, IsOk());
  EXPECT_NE(*h1, *h2);
  EXPECT_LT(*h1, *h2);
}

TEST_F(FakeNftablesTest, DeleteRuleByHandleRemovesOnlyThatRule) {
  Result<uint64_t> h1 = fake_.AddRule("ip", "t", "c", "content1", "a");
  ASSERT_THAT(h1, IsOk());
  ASSERT_THAT(fake_.AddRule("ip", "t", "c", "content2", "b"), IsOk());
  EXPECT_EQ(fake_.RuleCount("ip", "t", "c"), 2);

  EXPECT_THAT(fake_.DeleteRule("ip", "t", "c", *h1), IsOk());
  EXPECT_EQ(fake_.RuleCount("ip", "t", "c"), 1);
  EXPECT_FALSE(fake_.HasRuleWithComment("ip", "t", "c", "a"));
  EXPECT_TRUE(fake_.HasRuleWithComment("ip", "t", "c", "b"));
}

TEST_F(FakeNftablesTest, DeleteRuleWithUnknownHandleFails) {
  EXPECT_THAT(fake_.DeleteRule("ip", "t", "c", 9999), IsError());
}

TEST_F(FakeNftablesTest, DeleteRulesByCommentRemovesAllMatching) {
  ASSERT_THAT(fake_.AddRule("ip", "t", "c", "content1", "shared"), IsOk());
  ASSERT_THAT(fake_.AddRule("ip", "t", "c", "content2", "shared"), IsOk());
  ASSERT_THAT(fake_.AddRule("ip", "t", "c", "content3", "other"), IsOk());

  EXPECT_THAT(fake_.DeleteRulesByComment("ip", "t", "c", "shared"), IsOk());
  EXPECT_FALSE(fake_.HasRuleWithComment("ip", "t", "c", "shared"));
  EXPECT_TRUE(fake_.HasRuleWithComment("ip", "t", "c", "other"));
  EXPECT_EQ(fake_.RuleCount("ip", "t", "c"), 1);
}

TEST_F(FakeNftablesTest, DeleteRulesByCommentIsIdempotent) {
  // No matching comment, existing chain: no-op success.
  EXPECT_THAT(fake_.DeleteRulesByComment("ip", "t", "c", "nope"), IsOk());
  // Missing chain and missing table: still no-op success.
  EXPECT_THAT(fake_.DeleteRulesByComment("ip", "t", "missing", "x"), IsOk());
  EXPECT_THAT(fake_.DeleteRulesByComment("ip", "missing", "c", "x"), IsOk());
}

TEST_F(FakeNftablesTest, DeleteTableFailsWhenMissing) {
  EXPECT_THAT(fake_.DeleteTable("ip", "missing"), IsError());
}

TEST_F(FakeNftablesTest, RecreatingTableResetsHandleCounter) {
  Result<uint64_t> first = fake_.AddRule("ip", "t", "c", "content", "a");
  ASSERT_THAT(first, IsOk());

  ASSERT_THAT(fake_.DeleteTable("ip", "t"), IsOk());
  EXPECT_FALSE(fake_.HasTable("ip", "t"));

  ASSERT_THAT(fake_.EnsureTable("ip", "t"), IsOk());
  ASSERT_THAT(fake_.EnsureChain("ip", "t", "c", ""), IsOk());
  Result<uint64_t> again = fake_.AddRule("ip", "t", "c", "content", "a");
  ASSERT_THAT(again, IsOk());
  EXPECT_EQ(*first, *again);
}

TEST_F(FakeNftablesTest, TablesAreIsolatedByFamilyAndName) {
  ASSERT_THAT(fake_.EnsureTable("bridge", "t"), IsOk());
  ASSERT_THAT(fake_.EnsureChain("bridge", "t", "c", ""), IsOk());
  ASSERT_THAT(fake_.AddRule("ip", "t", "c", "content", "a"), IsOk());

  EXPECT_EQ(fake_.RuleCount("ip", "t", "c"), 1);
  EXPECT_EQ(fake_.RuleCount("bridge", "t", "c"), 0);
}

}  // namespace
}  // namespace cuttlefish
