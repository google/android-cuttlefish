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

#include <string_view>

#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kFamily = "ip";
constexpr std::string_view kTable = "t";
constexpr std::string_view kChain = "c";

class FakeNftablesTest : public ::testing::Test {
 protected:
  void CreateReadyChain() {
    ASSERT_THAT(fake_.EnsureTable(kFamily, kTable), IsOk());
    ASSERT_THAT(fake_.EnsureChain(kFamily, kTable, kChain, ""), IsOk());
  }

  FakeNftables fake_;
};

TEST_F(FakeNftablesTest, EnsureTableIsIdempotent) {
  ASSERT_THAT(fake_.EnsureTable(kFamily, kTable), IsOk());
  EXPECT_THAT(fake_.EnsureTable(kFamily, kTable), IsOk());
  EXPECT_TRUE(fake_.HasTable(kFamily, kTable));
}

TEST_F(FakeNftablesTest, EnsureChainIsIdempotent) {
  ASSERT_THAT(fake_.EnsureTable(kFamily, kTable), IsOk());
  EXPECT_THAT(fake_.EnsureChain(kFamily, kTable, kChain, ""), IsOk());
  EXPECT_THAT(fake_.EnsureChain(kFamily, kTable, kChain, ""), IsOk());
  EXPECT_TRUE(fake_.HasChain(kFamily, kTable, kChain));
}

TEST_F(FakeNftablesTest, EnsureChainRequiresTable) {
  EXPECT_THAT(fake_.EnsureChain(kFamily, "missing", kChain, ""), IsError());
}

TEST_F(FakeNftablesTest, AddRuleRequiresTableAndChain) {
  CreateReadyChain();
  EXPECT_THAT(fake_.AddRule(kFamily, "missing", kChain, "content", "cmt"),
              IsError());
  EXPECT_THAT(fake_.AddRule(kFamily, kTable, "missing", "content", "cmt"),
              IsError());
}

TEST_F(FakeNftablesTest, HandlesAreUniqueAndMonotonic) {
  CreateReadyChain();
  Result<uint64_t> h1 = fake_.AddRule(kFamily, kTable, kChain, "content1", "a");
  Result<uint64_t> h2 = fake_.AddRule(kFamily, kTable, kChain, "content2", "b");
  ASSERT_THAT(h1, IsOk());
  ASSERT_THAT(h2, IsOk());
  EXPECT_LT(*h1, *h2);
}

TEST_F(FakeNftablesTest, DeleteRuleByHandleRemovesOnlyThatRule) {
  CreateReadyChain();
  Result<uint64_t> h1 = fake_.AddRule(kFamily, kTable, kChain, "content1", "a");
  ASSERT_THAT(h1, IsOk());
  ASSERT_THAT(fake_.AddRule(kFamily, kTable, kChain, "content2", "b"), IsOk());
  EXPECT_EQ(fake_.RuleCount(kFamily, kTable, kChain), 2);

  EXPECT_THAT(fake_.DeleteRule(kFamily, kTable, kChain, *h1), IsOk());
  EXPECT_EQ(fake_.RuleCount(kFamily, kTable, kChain), 1);
  EXPECT_FALSE(fake_.HasRuleWithComment(kFamily, kTable, kChain, "a"));
  EXPECT_TRUE(fake_.HasRuleWithComment(kFamily, kTable, kChain, "b"));
}

TEST_F(FakeNftablesTest, DeleteRuleWithUnknownHandleFails) {
  CreateReadyChain();
  EXPECT_THAT(fake_.DeleteRule(kFamily, kTable, kChain, 9999), IsError());
}

TEST_F(FakeNftablesTest, DeleteRulesByCommentRemovesAllMatching) {
  CreateReadyChain();
  ASSERT_THAT(fake_.AddRule(kFamily, kTable, kChain, "content1", "shared"),
              IsOk());
  ASSERT_THAT(fake_.AddRule(kFamily, kTable, kChain, "content2", "shared"),
              IsOk());
  ASSERT_THAT(fake_.AddRule(kFamily, kTable, kChain, "content3", "other"),
              IsOk());

  EXPECT_THAT(fake_.DeleteRulesByComment(kFamily, kTable, kChain, "shared"),
              IsOk());
  EXPECT_FALSE(fake_.HasRuleWithComment(kFamily, kTable, kChain, "shared"));
  EXPECT_TRUE(fake_.HasRuleWithComment(kFamily, kTable, kChain, "other"));
  EXPECT_EQ(fake_.RuleCount(kFamily, kTable, kChain), 1);
}

TEST_F(FakeNftablesTest, DeleteRulesByCommentIsIdempotent) {
  CreateReadyChain();
  // No matching comment, existing chain: no-op success.
  EXPECT_THAT(fake_.DeleteRulesByComment(kFamily, kTable, kChain, "nope"),
              IsOk());
  // Missing chain and missing table: still no-op success.
  EXPECT_THAT(fake_.DeleteRulesByComment(kFamily, kTable, "missing", "x"),
              IsOk());
  EXPECT_THAT(fake_.DeleteRulesByComment(kFamily, "missing", kChain, "x"),
              IsOk());
}

TEST_F(FakeNftablesTest, DeleteTableFailsWhenMissing) {
  EXPECT_THAT(fake_.DeleteTable(kFamily, "missing"), IsError());
}

TEST_F(FakeNftablesTest, RecreatingTableResetsHandleCounter) {
  CreateReadyChain();
  Result<uint64_t> first =
      fake_.AddRule(kFamily, kTable, kChain, "content", "a");
  ASSERT_THAT(first, IsOk());

  ASSERT_THAT(fake_.DeleteTable(kFamily, kTable), IsOk());
  EXPECT_FALSE(fake_.HasTable(kFamily, kTable));

  CreateReadyChain();
  Result<uint64_t> again =
      fake_.AddRule(kFamily, kTable, kChain, "content", "a");
  EXPECT_THAT(again, IsOkAndValue(*first));
}

TEST_F(FakeNftablesTest, TablesAreIsolatedByFamilyAndName) {
  CreateReadyChain();
  ASSERT_THAT(fake_.EnsureTable("bridge", kTable), IsOk());
  ASSERT_THAT(fake_.EnsureChain("bridge", kTable, kChain, ""), IsOk());
  ASSERT_THAT(fake_.AddRule(kFamily, kTable, kChain, "content", "a"), IsOk());

  EXPECT_EQ(fake_.RuleCount(kFamily, kTable, kChain), 1);
  EXPECT_EQ(fake_.RuleCount("bridge", kTable, kChain), 0);
}

}  // namespace
}  // namespace cuttlefish
