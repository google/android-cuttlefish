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

#include "allocd/net/nft_messages.h"

#include <errno.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netlink.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "allocd/net/netlink_request.h"
#include "allocd/test/nft_messages_helper.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using nft_test::Attr;
using nft_test::BuildErrorReply;
using nft_test::BuildTableReply;
using nft_test::Concat;
using nft_test::Expression;
using nft_test::GetIpv4AddressAttr;
using nft_test::GetNestedAttrs;
using nft_test::GetRuleExpressions;
using nft_test::GetStringAttr;
using nft_test::GetUint32BigEndianAttr;
using nft_test::Message;
using nft_test::NftType;
using nft_test::ParseMessages;
using testing::HasSubstr;

TEST(NftMessagesTest, NewTableIsOwnedAndExclusive) {
  const std::vector<Message> messages =
      ParseMessages(nft::NewTable("cvdalloc_1"));
  ASSERT_EQ(messages.size(), 1u);
  const Message& table = messages[0];
  EXPECT_EQ(table.type, NftType(NFT_MSG_NEWTABLE));
  EXPECT_EQ(table.family, NFPROTO_IPV4);
  EXPECT_TRUE(table.flags & NLM_F_CREATE);
  EXPECT_TRUE(table.flags & NLM_F_EXCL);
  EXPECT_EQ(GetStringAttr(table.attrs, NFTA_TABLE_NAME), "cvdalloc_1");
  EXPECT_EQ(GetUint32BigEndianAttr(table.attrs, NFTA_TABLE_FLAGS),
            static_cast<uint32_t>(NFT_TABLE_F_OWNER));
}

TEST(NftMessagesTest, DeleteTableNamesTable) {
  const std::vector<Message> messages =
      ParseMessages(nft::DeleteTable("cvdalloc_1"));
  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages[0].type, NftType(NFT_MSG_DELTABLE));
  EXPECT_EQ(messages[0].family, NFPROTO_IPV4);
  EXPECT_EQ(GetStringAttr(messages[0].attrs, NFTA_TABLE_NAME), "cvdalloc_1");
}

TEST(NftMessagesTest, GetTableLooksUpOneTable) {
  const std::vector<Message> messages =
      ParseMessages(nft::GetTable("cvdalloc_1"));
  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages[0].type, NftType(NFT_MSG_GETTABLE));
  EXPECT_EQ(messages[0].family, NFPROTO_IPV4);
  // a dump would return every table, so any table would count as found
  EXPECT_FALSE(messages[0].flags & NLM_F_DUMP);
  EXPECT_EQ(GetStringAttr(messages[0].attrs, NFTA_TABLE_NAME), "cvdalloc_1");
}

TEST(NftMessagesTest, NewNatChainHooksPostroutingAtSourceNatPriority) {
  const std::vector<Message> messages =
      ParseMessages(nft::NewNatChain("cvdalloc_1", "postrouting"));
  ASSERT_EQ(messages.size(), 1u);
  const Message& chain = messages[0];
  EXPECT_EQ(chain.type, NftType(NFT_MSG_NEWCHAIN));
  EXPECT_EQ(chain.family, NFPROTO_IPV4);
  EXPECT_TRUE(chain.flags & NLM_F_CREATE);
  EXPECT_EQ(GetStringAttr(chain.attrs, NFTA_CHAIN_TABLE), "cvdalloc_1");
  EXPECT_EQ(GetStringAttr(chain.attrs, NFTA_CHAIN_NAME), "postrouting");
  EXPECT_EQ(GetStringAttr(chain.attrs, NFTA_CHAIN_TYPE), "nat");
  const std::vector<Attr> hook = GetNestedAttrs(chain.attrs, NFTA_CHAIN_HOOK);
  EXPECT_EQ(GetUint32BigEndianAttr(hook, NFTA_HOOK_HOOKNUM),
            static_cast<uint32_t>(NF_INET_POST_ROUTING));
  EXPECT_EQ(GetUint32BigEndianAttr(hook, NFTA_HOOK_PRIORITY),
            static_cast<uint32_t>(NF_IP_PRI_NAT_SRC));
}

TEST(NftMessagesTest, NewMasqueradeRuleMatchesSourceNetwork) {
  struct Case {
    std::string source;
    std::string address;
    std::string mask;
  };
  const Case cases[] = {
      {"0.0.0.0/0", "0.0.0.0", "0.0.0.0"},
      {"192.168.144.0/30", "192.168.144.0", "255.255.255.252"},
      {"192.168.160.0/24", "192.168.160.0", "255.255.255.0"},
  };
  for (const Case& c : cases) {
    SCOPED_TRACE(c.source);
    const Result<NetlinkRequest> rule =
        nft::NewMasqueradeRule("cvdalloc_1", "postrouting", c.source);
    ASSERT_THAT(rule, IsOk());
    const std::vector<Message> messages = ParseMessages(*rule);
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].type, NftType(NFT_MSG_NEWRULE));
    EXPECT_EQ(messages[0].family, NFPROTO_IPV4);
    EXPECT_TRUE(messages[0].flags & NLM_F_CREATE);
    EXPECT_TRUE(messages[0].flags & NLM_F_APPEND);
    EXPECT_EQ(GetStringAttr(messages[0].attrs, NFTA_RULE_TABLE), "cvdalloc_1");
    EXPECT_EQ(GetStringAttr(messages[0].attrs, NFTA_RULE_CHAIN), "postrouting");

    const std::vector<Expression> expressions = GetRuleExpressions(messages[0]);
    ASSERT_EQ(expressions.size(), 4u);
    const auto& [payload_name, payload] = expressions[0];
    const auto& [bitwise_name, bitwise] = expressions[1];
    const auto& [cmp_name, cmp] = expressions[2];
    EXPECT_EQ(payload_name, "payload");
    EXPECT_EQ(GetUint32BigEndianAttr(payload, NFTA_PAYLOAD_BASE),
              static_cast<uint32_t>(NFT_PAYLOAD_NETWORK_HEADER));
    EXPECT_EQ(GetUint32BigEndianAttr(payload, NFTA_PAYLOAD_OFFSET), 12u);
    EXPECT_EQ(GetUint32BigEndianAttr(payload, NFTA_PAYLOAD_LEN), 4u);
    EXPECT_EQ(bitwise_name, "bitwise");
    EXPECT_EQ(GetUint32BigEndianAttr(bitwise, NFTA_BITWISE_LEN), 4u);
    EXPECT_EQ(GetIpv4AddressAttr(bitwise, NFTA_BITWISE_MASK), c.mask);
    EXPECT_EQ(GetIpv4AddressAttr(bitwise, NFTA_BITWISE_XOR), "0.0.0.0");
    EXPECT_EQ(cmp_name, "cmp");
    EXPECT_EQ(GetUint32BigEndianAttr(cmp, NFTA_CMP_OP),
              static_cast<uint32_t>(NFT_CMP_EQ));
    EXPECT_EQ(GetIpv4AddressAttr(cmp, NFTA_CMP_DATA), c.address);
    EXPECT_EQ(expressions[3].name, "masq");

    const uint32_t reg = GetUint32BigEndianAttr(payload, NFTA_PAYLOAD_DREG);
    EXPECT_EQ(GetUint32BigEndianAttr(bitwise, NFTA_BITWISE_SREG), reg);
    EXPECT_EQ(GetUint32BigEndianAttr(bitwise, NFTA_BITWISE_DREG), reg);
    EXPECT_EQ(GetUint32BigEndianAttr(cmp, NFTA_CMP_SREG), reg);
  }
}

TEST(NftMessagesTest, NewMasqueradeRuleClearsHostBits) {
  const Result<NetlinkRequest> rule =
      nft::NewMasqueradeRule("cvdalloc_1", "postrouting", "192.168.144.3/30");
  ASSERT_THAT(rule, IsOk());
  const std::vector<Message> messages = ParseMessages(*rule);
  ASSERT_EQ(messages.size(), 1u);
  const std::vector<Expression> expressions = GetRuleExpressions(messages[0]);
  ASSERT_EQ(expressions.size(), 4u);
  EXPECT_EQ(GetIpv4AddressAttr(expressions[2].attrs, NFTA_CMP_DATA),
            "192.168.144.0");
}

TEST(NftMessagesTest, NewMasqueradeRuleRejectsInvalidSource) {
  for (const std::string_view source :
       {"192.168.144.0", "192.168.144.0/30/1", "192.168.144.300/30",
        "192.168.144.0/-1", "192.168.144.0/33", "192.168.144.0/x",
        "network/24"}) {
    SCOPED_TRACE(source);
    EXPECT_THAT(nft::NewMasqueradeRule("cvdalloc_1", "postrouting", source),
                IsError());
  }
}

TEST(NftMessagesTest, BatchWrapsChangesInBeginAndEnd) {
  std::vector<NetlinkRequest> changes;
  changes.push_back(nft::NewTable("cvdalloc_1"));
  changes.push_back(nft::DeleteTable("cvdalloc_1"));
  const std::vector<Message> batch = ParseMessages(nft::Batch(changes));
  ASSERT_EQ(batch.size(), 4u);
  EXPECT_EQ(batch[0].type, NFNL_MSG_BATCH_BEGIN);
  EXPECT_EQ(batch[0].res_id, NFNL_SUBSYS_NFTABLES);
  EXPECT_EQ(batch[1].type, NftType(NFT_MSG_NEWTABLE));
  EXPECT_EQ(batch[2].type, NftType(NFT_MSG_DELTABLE));
  EXPECT_EQ(batch[3].type, NFNL_MSG_BATCH_END);
  EXPECT_EQ(batch[3].res_id, NFNL_SUBSYS_NFTABLES);
}

TEST(NftMessagesTest, CheckRepliesAcceptsAcks) {
  EXPECT_THAT(nft::CheckReplies(Concat(BuildErrorReply(0), BuildErrorReply(0))),
              IsOk());
}

TEST(NftMessagesTest, CheckRepliesFailsOnAnyError) {
  EXPECT_THAT(
      nft::CheckReplies(Concat(BuildErrorReply(0), BuildErrorReply(EPERM))),
      IsErrorAndMessage(HasSubstr("Operation not permitted")));
}

TEST(NftMessagesTest, CheckRepliesFailsOnTruncatedReply) {
  std::vector<char> reply = BuildErrorReply(0);
  reply.pop_back();
  EXPECT_THAT(nft::CheckReplies(reply), IsError());
}

TEST(NftMessagesTest, CheckRepliesFailsWithoutReply) {
  EXPECT_THAT(nft::CheckReplies({}), IsError());
}

TEST(NftMessagesTest, TableExistsFromReplyTrueOnTable) {
  EXPECT_THAT(nft::TableExistsFromReply(
                  Concat(BuildTableReply("cvdalloc_1"), BuildErrorReply(0))),
              IsOkAndValue(true));
}

TEST(NftMessagesTest, TableExistsFromReplyFalseOnEnoent) {
  EXPECT_THAT(nft::TableExistsFromReply(BuildErrorReply(ENOENT)),
              IsOkAndValue(false));
}

TEST(NftMessagesTest, TableExistsFromReplyFailsOnOtherError) {
  EXPECT_THAT(nft::TableExistsFromReply(BuildErrorReply(EPERM)), IsError());
}

TEST(NftMessagesTest, TableExistsFromReplyFailsWithoutReply) {
  EXPECT_THAT(nft::TableExistsFromReply({}), IsError());
}

}  // namespace
}  // namespace cuttlefish
