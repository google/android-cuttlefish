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

#include "allocd/net/nft_nat_table.h"

#include <errno.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netlink.h>  // IWYU pragma: keep
#include <stddef.h>
#include <stdint.h>

#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "allocd/test/fake_nfnetlink_socket.h"
#include "allocd/test/nft_messages_helper.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using nft_test::BuildErrorReply;
using nft_test::BuildTableReply;
using nft_test::Concat;
using nft_test::Expression;
using nft_test::GetIpv4AddressAttr;
using nft_test::GetRuleExpressions;
using nft_test::GetStringAttr;
using nft_test::Message;
using nft_test::NftType;
using nft_test::ParseMessages;
using testing::ElementsAre;
using testing::HasSubstr;

std::vector<uint16_t> Types(const std::vector<Message>& messages) {
  std::vector<uint16_t> types;
  for (const Message& message : messages) {
    types.push_back(message.type);
  }
  return types;
}

void QueueCreateAndDeleteAcks(FakeNfnetlinkSocket::State& state) {
  state.replies.push_back(BuildErrorReply(0));
  state.replies.push_back(BuildErrorReply(0));
}

TEST(NftNatTableTest, CreateSendsTableChainAndRulesInOneBatch) {
  const std::string table_name = "cvdalloc_1";
  auto state = std::make_shared<FakeNfnetlinkSocket::State>();
  QueueCreateAndDeleteAcks(*state);
  const Result<NftNatTable> table =
      NftNatTable::Create(std::make_unique<FakeNfnetlinkSocket>(state),
                          table_name, {"192.168.144.0/30", "192.168.160.0/24"});
  ASSERT_THAT(table, IsOk());
  ASSERT_EQ(state->sent.size(), 1u);

  const std::vector<Message> batch = ParseMessages(state->sent[0]);
  ASSERT_THAT(Types(batch),
              ElementsAre(NFNL_MSG_BATCH_BEGIN, NftType(NFT_MSG_NEWTABLE),
                          NftType(NFT_MSG_NEWCHAIN), NftType(NFT_MSG_NEWRULE),
                          NftType(NFT_MSG_NEWRULE), NFNL_MSG_BATCH_END));
  EXPECT_EQ(GetStringAttr(batch[1].attrs, NFTA_TABLE_NAME), table_name);
  EXPECT_EQ(GetStringAttr(batch[2].attrs, NFTA_CHAIN_TABLE), table_name);
  EXPECT_EQ(GetStringAttr(batch[2].attrs, NFTA_CHAIN_NAME), "postrouting");

  const std::string networks[] = {"192.168.144.0", "192.168.160.0"};
  constexpr size_t kFirstRuleFoundAt = 3;
  for (size_t i = 0; i < std::size(networks); i++) {
    SCOPED_TRACE(i);
    const Message& rule = batch[kFirstRuleFoundAt + i];
    EXPECT_EQ(GetStringAttr(rule.attrs, NFTA_RULE_TABLE), table_name);
    EXPECT_EQ(GetStringAttr(rule.attrs, NFTA_RULE_CHAIN), "postrouting");
    const std::vector<Expression> expressions = GetRuleExpressions(rule);
    ASSERT_EQ(expressions.size(), 4u);
    EXPECT_EQ(GetIpv4AddressAttr(expressions[2].attrs, NFTA_CMP_DATA),
              networks[i]);
  }
}

TEST(NftNatTableTest, CreateRejectsInvalidSourceBeforeSending) {
  auto state = std::make_shared<FakeNfnetlinkSocket::State>();
  const Result<NftNatTable> table =
      NftNatTable::Create(std::make_unique<FakeNfnetlinkSocket>(state),
                          "cvdalloc_1", {"192.168.144.0/30", "network/24"});
  EXPECT_THAT(table, IsError());
  EXPECT_TRUE(state->sent.empty());
}

TEST(NftNatTableTest, CreateFailsWhenKernelRejects) {
  auto state = std::make_shared<FakeNfnetlinkSocket::State>();
  state->replies.push_back(BuildErrorReply(EPERM));
  const Result<NftNatTable> table =
      NftNatTable::Create(std::make_unique<FakeNfnetlinkSocket>(state),
                          "cvdalloc_1", {"192.168.144.0/30"});
  EXPECT_THAT(table, IsErrorAndMessage(HasSubstr("Operation not permitted")));
  // There is nothing to delete after a failed create.
  EXPECT_EQ(state->sent.size(), 1u);
}

TEST(NftNatTableTest, DestructorDeletesTable) {
  const std::string table_name = "cvdalloc_1";
  auto state = std::make_shared<FakeNfnetlinkSocket::State>();
  QueueCreateAndDeleteAcks(*state);
  {
    const Result<NftNatTable> table =
        NftNatTable::Create(std::make_unique<FakeNfnetlinkSocket>(state),
                            table_name, {"192.168.144.0/30"});
    ASSERT_THAT(table, IsOk());
    EXPECT_EQ(state->sent.size(), 1u);
  }
  ASSERT_EQ(state->sent.size(), 2u);

  const std::vector<Message> batch = ParseMessages(state->sent[1]);
  ASSERT_THAT(Types(batch),
              ElementsAre(NFNL_MSG_BATCH_BEGIN, NftType(NFT_MSG_DELTABLE),
                          NFNL_MSG_BATCH_END));
  EXPECT_EQ(GetStringAttr(batch[1].attrs, NFTA_TABLE_NAME), table_name);
}

TEST(NftNatTableTest, MoveTransfersOwnership) {
  auto state = std::make_shared<FakeNfnetlinkSocket::State>();
  QueueCreateAndDeleteAcks(*state);
  std::optional<NftNatTable> moved;
  {
    Result<NftNatTable> table =
        NftNatTable::Create(std::make_unique<FakeNfnetlinkSocket>(state),
                            "cvdalloc_1", {"192.168.144.0/30"});
    ASSERT_THAT(table, IsOk());
    moved.emplace(std::move(*table));
  }
  EXPECT_EQ(state->sent.size(), 1u);
  moved.reset();
  EXPECT_EQ(state->sent.size(), 2u);
}

TEST(NftNatTableTest, ExistsTrueOnTableReply) {
  const std::string table_name = "cvdalloc_1";
  auto state = std::make_shared<FakeNfnetlinkSocket::State>();
  state->replies.push_back(
      Concat(BuildTableReply(table_name), BuildErrorReply(0)));
  FakeNfnetlinkSocket socket(state);
  EXPECT_THAT(NftNatTable::Exists(socket, table_name), IsOkAndValue(true));

  ASSERT_EQ(state->sent.size(), 1u);
  const std::vector<Message> messages = ParseMessages(state->sent[0]);
  ASSERT_THAT(Types(messages), ElementsAre(NftType(NFT_MSG_GETTABLE)));
  EXPECT_EQ(GetStringAttr(messages[0].attrs, NFTA_TABLE_NAME), table_name);
}

TEST(NftNatTableTest, ExistsFalseOnEnoent) {
  auto state = std::make_shared<FakeNfnetlinkSocket::State>();
  state->replies.push_back(BuildErrorReply(ENOENT));
  FakeNfnetlinkSocket socket(state);
  EXPECT_THAT(NftNatTable::Exists(socket, "cvdalloc_1"), IsOkAndValue(false));
}

TEST(NftNatTableTest, ExistsFailsOnError) {
  auto state = std::make_shared<FakeNfnetlinkSocket::State>();
  state->replies.push_back(BuildErrorReply(EPERM));
  FakeNfnetlinkSocket socket(state);
  EXPECT_THAT(NftNatTable::Exists(socket, "cvdalloc_1"), IsError());
}

}  // namespace
}  // namespace cuttlefish
