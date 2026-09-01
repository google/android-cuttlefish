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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string_view>
#include <utility>

#include "allocd/alloc_utils.h"
#include "allocd/net/nft_rule.h"
#include "allocd/test/fake_nftables.h"
#include "allocd/test/mock_nftables.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::_;
using ::testing::Return;

TEST(AllocUtilsFirewallTest, SetupFirewallSuccess) {
  MockNftables mock_nft;

  EXPECT_CALL(mock_nft, DeleteTable("ip", "cf_cvdalloc_nat"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, DeleteTable("bridge", "cf_cvdalloc_bridge"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, EnsureTable("ip", "cf_cvdalloc_nat"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, EnsureChain("ip", "cf_cvdalloc_nat", "postrouting", _))
      .WillOnce(Return(Result<void>{}));

  EXPECT_CALL(mock_nft, AddRule("ip", "cf_cvdalloc_nat", "postrouting", _))
      .Times(4)
      .WillRepeatedly(Return(1));

  EXPECT_THAT(SetupFirewall(mock_nft, /*setup_byob=*/false), IsOk());
}

TEST(AllocUtilsFirewallTest, SetupFirewallWithByob) {
  MockNftables mock_nft;

  EXPECT_CALL(mock_nft, DeleteTable("ip", "cf_cvdalloc_nat"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, DeleteTable("bridge", "cf_cvdalloc_bridge"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, EnsureTable("ip", "cf_cvdalloc_nat"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, EnsureChain("ip", "cf_cvdalloc_nat", "postrouting", _))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, AddRule("ip", "cf_cvdalloc_nat", "postrouting", _))
      .Times(4)
      .WillRepeatedly(Return(1));

  EXPECT_CALL(mock_nft, EnsureTable("bridge", "cf_cvdalloc_bridge"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft,
              EnsureChain("bridge", "cf_cvdalloc_bridge", "prerouting", _))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft,
              EnsureChain("bridge", "cf_cvdalloc_bridge", "forward", _))
      .WillOnce(Return(Result<void>{}));

  EXPECT_THAT(SetupFirewall(mock_nft, /*setup_byob=*/true), IsOk());
}

TEST(AllocUtilsFirewallTest, TeardownFirewallDeletesTables) {
  MockNftables mock_nft;

  EXPECT_CALL(mock_nft, DeleteTable("ip", "cf_cvdalloc_nat"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, DeleteTable("bridge", "cf_cvdalloc_bridge"))
      .WillOnce(Return(Result<void>{}));

  EXPECT_THAT(TeardownFirewall(mock_nft), IsOk());
}

constexpr std::string_view kFamily = "ip";
constexpr std::string_view kTable = "cf_cvdalloc_nat";
constexpr std::string_view kChain = "postrouting";

// Regression test for stale nft handles surviving a table recreation. Before
// the comment-based teardown, a winding-down instance would delete another
// instance's rule that had been assigned the same reused handle number. See
// FakeNftables for the modeled handle-counter reset behavior.
TEST(AllocUtilsFirewallTest, StaleHandleDoesNotDeleteAnotherInstancesRule) {
  FakeNftables fake;

  // Host setup installs the static rules and creates the nat table.
  ASSERT_THAT(SetupFirewall(fake, /*setup_byob=*/false), IsOk());

  // Instance A adds its dynamic masquerade rule and keeps the NftRule alive,
  // as the long-lived per-instance cvdalloc process does.
  Result<NftRule> a =
      NftRule::Create(fake, kFamily, kTable, kChain,
                      "ip saddr 192.168.97.0/30 masquerade", "mtap-01");
  ASSERT_THAT(a, IsOk());
  std::optional<NftRule> rule_a(std::move(*a));
  ASSERT_TRUE(fake.HasRuleWithComment("cvdalloc-mtap-01"));

  // The host firewall is re-setup (idempotent --setup), deleting and recreating
  // the table. This drops A's live rule AND resets the nft handle counter.
  ASSERT_THAT(SetupFirewall(fake, /*setup_byob=*/false), IsOk());
  ASSERT_FALSE(fake.HasRuleWithComment("cvdalloc-mtap-01"));

  // Instance B adds its rule, which is assigned the SAME handle number that A
  // still has cached, due to the counter reset.
  Result<NftRule> b =
      NftRule::Create(fake, kFamily, kTable, kChain,
                      "ip saddr 192.168.97.4/30 masquerade", "mtap-02");
  ASSERT_THAT(b, IsOk());
  ASSERT_TRUE(fake.HasRuleWithComment("cvdalloc-mtap-02"));

  // Instance A winds down. A stale handle-based delete would remove B's rule;
  // comment-based deletion finds nothing for A and leaves B untouched.
  rule_a.reset();

  EXPECT_TRUE(fake.HasRuleWithComment("cvdalloc-mtap-02"))
      << "Instance A's teardown deleted instance B's rule";
}

}  // namespace
}  // namespace cuttlefish
