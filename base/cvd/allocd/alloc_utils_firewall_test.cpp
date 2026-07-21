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

#include "allocd/alloc_utils.h"
#include "allocd/test/mock_nftables.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::_;
using ::testing::Return;

TEST(AllocUtilsFirewallTest, SetupFirewallSuccess) {
  MockNftables mock_nft;

  EXPECT_CALL(mock_nft, EnsureTable("ip", "cuttlefish_nat"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, EnsureChain("ip", "cuttlefish_nat", "postrouting", _))
      .WillOnce(Return(Result<void>{}));

  EXPECT_CALL(mock_nft, AddRule("ip", "cuttlefish_nat", "postrouting", _))
      .Times(4)
      .WillRepeatedly(Return(1));

  EXPECT_THAT(SetupFirewall(mock_nft, /*setup_byob=*/false), IsOk());
}

TEST(AllocUtilsFirewallTest, SetupFirewallWithByob) {
  MockNftables mock_nft;

  EXPECT_CALL(mock_nft, EnsureTable("ip", "cuttlefish_nat"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, EnsureChain("ip", "cuttlefish_nat", "postrouting", _))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, AddRule("ip", "cuttlefish_nat", "postrouting", _))
      .Times(4)
      .WillRepeatedly(Return(1));

  EXPECT_CALL(mock_nft, EnsureTable("bridge", "cuttlefish_bridge"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, EnsureChain("bridge", "cuttlefish_bridge", "prerouting", _))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, EnsureChain("bridge", "cuttlefish_bridge", "forward", _))
      .WillOnce(Return(Result<void>{}));

  EXPECT_THAT(SetupFirewall(mock_nft, /*setup_byob=*/true), IsOk());
}

TEST(AllocUtilsFirewallTest, TeardownFirewallDeletesTables) {
  MockNftables mock_nft;

  EXPECT_CALL(mock_nft, DeleteTable("ip", "cuttlefish_nat"))
      .WillOnce(Return(Result<void>{}));
  EXPECT_CALL(mock_nft, DeleteTable("bridge", "cuttlefish_bridge"))
      .WillOnce(Return(Result<void>{}));

  EXPECT_THAT(TeardownFirewall(mock_nft), IsOk());
}

}  // namespace
}  // namespace cuttlefish
