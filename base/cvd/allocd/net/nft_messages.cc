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

#include <arpa/inet.h>
#include <errno.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netlink.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "allocd/net/netlink_request.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish::nft {
namespace {

// both in network byte order
struct Ipv4Network {
  in_addr_t address;
  in_addr_t mask;
};

// e.g. 24 -> 0xffffff00
uint32_t PrefixLengthToMask(int prefix_length) {
  // shifting a 32 bit value by 32 is undefined
  if (prefix_length == 0) {
    return 0;
  }
  return UINT32_MAX << (32 - prefix_length);
}

// e.g. "192.168.144.0/30" -> {192.168.144.0, 255.255.255.252}
Result<Ipv4Network> ParseIpv4Network(std::string_view cidr) {
  const std::vector<std::string_view> parts = absl::StrSplit(cidr, '/');
  CF_EXPECTF(parts.size() == 2, "Expected <address>/<prefix length>, got '{}'",
             cidr);

  in_addr address{};
  CF_EXPECTF(inet_pton(AF_INET, std::string(parts[0]).c_str(), &address) == 1,
             "Invalid IPv4 address in '{}'", cidr);

  int prefix_length = -1;
  CF_EXPECTF(absl::SimpleAtoi(parts[1], &prefix_length),
             "Invalid prefix length in '{}'", cidr);
  CF_EXPECTF(prefix_length >= 0 && prefix_length <= 32,
             "Prefix length out of range in '{}'", cidr);

  const in_addr_t mask = htonl(PrefixLengthToMask(prefix_length));
  // the rule compares the masked source address with this, so the host bits
  // must be zero
  return Ipv4Network{
      .address = address.s_addr & mask,
      .mask = mask,
  };
}

// an nf_tables message is a netlink header, then an nfgenmsg, then attributes

// subsystem in the high byte, NFT_MSG_* in the low byte
uint16_t NftType(uint16_t nft_message) {
  return (NFNL_SUBSYS_NFTABLES << 8) | nft_message;
}

NetlinkRequest CreateNftRequest(uint16_t nft_message, uint16_t flags) {
  NetlinkRequest req(NftType(nft_message), flags);
  req.Append(nfgenmsg{
      .nfgen_family = NFPROTO_IPV4,
      .version = NFNETLINK_V0,
      .res_id = 0,
  });
  return req;
}

// batch begin and end carry the subsystem the batch is for in res_id
NetlinkRequest CreateBatchDelimiter(uint16_t type) {
  NetlinkRequest req(type, 0);
  req.Append(nfgenmsg{
      .nfgen_family = AF_UNSPEC,
      .version = NFNETLINK_V0,
      .res_id = htons(NFNL_SUBSYS_NFTABLES),
  });
  return req;
}

// nf_tables integer attributes use network (big-endian) byte order
void AddUint32BigEndian(NetlinkRequest& req, uint16_t type, uint32_t value) {
  req.AddInt(type, htonl(value));
}

void AddString(NetlinkRequest& req, uint16_t type, std::string_view value) {
  req.AddString(type, std::string(value));
}

// the kernel expects expression constants wrapped in a nested NFTA_DATA_VALUE
// attribute
void AddNestedDataValue(NetlinkRequest& req, uint16_t type, in_addr_t value) {
  req.PushList(type | NLA_F_NESTED);
  req.AddInAddr(NFTA_DATA_VALUE, &value);
  req.PopList();
}

// a rule is a list of expressions that the kernel runs in order on each
// packet, passing values between them in registers
// ip saddr <network> masquerade becomes
//   payload   read the packet's source address into a register
//   bitwise   mask the register with the network mask
//   cmp       end this rule unless the register equals the network address
//   masq      masquerade the packet

void BeginExpression(NetlinkRequest& req, std::string_view name) {
  req.PushList(NFTA_LIST_ELEM | NLA_F_NESTED);
  AddString(req, NFTA_EXPR_NAME, name);
  req.PushList(NFTA_EXPR_DATA | NLA_F_NESTED);
}

void EndExpression(NetlinkRequest& req) {
  req.PopList();
  req.PopList();
}

void ReadSourceAddressIntoRegister(NetlinkRequest& req, uint32_t reg) {
  BeginExpression(req, "payload");
  AddUint32BigEndian(req, NFTA_PAYLOAD_DREG, reg);
  AddUint32BigEndian(req, NFTA_PAYLOAD_BASE, NFT_PAYLOAD_NETWORK_HEADER);
  AddUint32BigEndian(req, NFTA_PAYLOAD_OFFSET, offsetof(iphdr, saddr));
  AddUint32BigEndian(req, NFTA_PAYLOAD_LEN, sizeof(in_addr_t));
  EndExpression(req);
}

void MaskRegister(NetlinkRequest& req, uint32_t reg, in_addr_t mask) {
  BeginExpression(req, "bitwise");
  AddUint32BigEndian(req, NFTA_BITWISE_SREG, reg);
  AddUint32BigEndian(req, NFTA_BITWISE_DREG, reg);
  AddUint32BigEndian(req, NFTA_BITWISE_LEN, sizeof(in_addr_t));
  AddNestedDataValue(req, NFTA_BITWISE_MASK, mask);
  AddNestedDataValue(req, NFTA_BITWISE_XOR, 0);
  EndExpression(req);
}

void CompareRegisterEquals(NetlinkRequest& req, uint32_t reg, in_addr_t value) {
  BeginExpression(req, "cmp");
  AddUint32BigEndian(req, NFTA_CMP_SREG, reg);
  AddUint32BigEndian(req, NFTA_CMP_OP, NFT_CMP_EQ);
  AddNestedDataValue(req, NFTA_CMP_DATA, value);
  EndExpression(req);
}

void AddMasqueradeExpression(NetlinkRequest& req) {
  BeginExpression(req, "masq");
  EndExpression(req);
}

void Append(std::vector<char>& out, const NetlinkRequest& message) {
  const char* data = static_cast<const char*>(message.RequestData());
  out.insert(out.end(), data, data + message.RequestLength());
}

struct Reply {
  uint16_t type;
  // negative errno, only set for NLMSG_ERROR, where 0 is an ack
  int error;
};

// raw netlink bytes -> replies
Result<std::vector<Reply>> UnpackReplies(const std::vector<char>& reply_bytes) {
  std::vector<Reply> replies;
  size_t offset = 0;
  while (offset < reply_bytes.size()) {
    nlmsghdr header{};
    CF_EXPECT(reply_bytes.size() - offset >= sizeof(header),
              "Truncated netlink reply");
    memcpy(&header, reply_bytes.data() + offset, sizeof(header));
    CF_EXPECT(header.nlmsg_len >= sizeof(header) &&
                  header.nlmsg_len <= reply_bytes.size() - offset,
              "Truncated netlink reply");
    Reply reply{header.nlmsg_type, 0};
    if (header.nlmsg_type == NLMSG_ERROR) {
      CF_EXPECT(header.nlmsg_len >= sizeof(header) + sizeof(reply.error),
                "Truncated netlink error");
      memcpy(&reply.error, reply_bytes.data() + offset + sizeof(header),
             sizeof(reply.error));
    }
    replies.push_back(reply);
    offset += NLMSG_ALIGN(header.nlmsg_len);
  }
  return replies;
}

}  // namespace

NetlinkRequest NewTable(std::string_view name) {
  NetlinkRequest req =
      CreateNftRequest(NFT_MSG_NEWTABLE, NLM_F_CREATE | NLM_F_EXCL);
  AddString(req, NFTA_TABLE_NAME, name);
  AddUint32BigEndian(req, NFTA_TABLE_FLAGS, NFT_TABLE_F_OWNER);
  return req;
}

NetlinkRequest DeleteTable(std::string_view name) {
  NetlinkRequest req = CreateNftRequest(NFT_MSG_DELTABLE, 0);
  AddString(req, NFTA_TABLE_NAME, name);
  return req;
}

NetlinkRequest NewNatChain(std::string_view table, std::string_view chain) {
  NetlinkRequest req = CreateNftRequest(NFT_MSG_NEWCHAIN, NLM_F_CREATE);
  AddString(req, NFTA_CHAIN_TABLE, table);
  AddString(req, NFTA_CHAIN_NAME, chain);
  req.PushList(NFTA_CHAIN_HOOK | NLA_F_NESTED);
  AddUint32BigEndian(req, NFTA_HOOK_HOOKNUM, NF_INET_POST_ROUTING);
  AddUint32BigEndian(req, NFTA_HOOK_PRIORITY, NF_IP_PRI_NAT_SRC);
  req.PopList();
  AddString(req, NFTA_CHAIN_TYPE, "nat");
  return req;
}

Result<NetlinkRequest> NewMasqueradeRule(std::string_view table,
                                         std::string_view chain,
                                         std::string_view source_cidr) {
  const Ipv4Network network = CF_EXPECT(ParseIpv4Network(source_cidr));
  NetlinkRequest req =
      CreateNftRequest(NFT_MSG_NEWRULE, NLM_F_CREATE | NLM_F_APPEND);
  AddString(req, NFTA_RULE_TABLE, table);
  AddString(req, NFTA_RULE_CHAIN, chain);
  req.PushList(NFTA_RULE_EXPRESSIONS | NLA_F_NESTED);
  ReadSourceAddressIntoRegister(req, NFT_REG_1);
  MaskRegister(req, NFT_REG_1, network.mask);
  CompareRegisterEquals(req, NFT_REG_1, network.address);
  AddMasqueradeExpression(req);
  req.PopList();
  return req;
}

std::vector<char> Batch(const std::vector<NetlinkRequest>& changes) {
  std::vector<char> out;
  Append(out, CreateBatchDelimiter(NFNL_MSG_BATCH_BEGIN));
  for (const NetlinkRequest& change : changes) {
    Append(out, change);
  }
  Append(out, CreateBatchDelimiter(NFNL_MSG_BATCH_END));
  return out;
}

Result<void> CheckReplies(const std::vector<char>& reply_bytes) {
  const std::vector<Reply> replies = CF_EXPECT(UnpackReplies(reply_bytes));
  // NetlinkRequest asks the kernel to ack every message, so getting no reply
  // at all means something went wrong
  CF_EXPECT(!replies.empty(), "No reply to the request");
  for (const Reply& reply : replies) {
    if (reply.type == NLMSG_ERROR) {
      CF_EXPECTF(reply.error == 0, "nftables rejected the request: {}",
                 StrError(-reply.error));
    }
  }
  return {};
}

std::vector<char> GetTable(std::string_view name) {
  NetlinkRequest req = CreateNftRequest(NFT_MSG_GETTABLE, 0);
  AddString(req, NFTA_TABLE_NAME, name);
  std::vector<char> bytes;
  Append(bytes, req);
  return bytes;
}

Result<bool> TableExistsFromReply(const std::vector<char>& reply_bytes) {
  for (const Reply& reply : CF_EXPECT(UnpackReplies(reply_bytes))) {
    if (reply.type == NftType(NFT_MSG_NEWTABLE)) {
      return true;
    }
    if (reply.type == NLMSG_ERROR) {
      if (reply.error == -ENOENT) {
        return false;
      }
      CF_EXPECTF(reply.error == 0, "nftables rejected the lookup: {}",
                 StrError(-reply.error));
    }
  }
  return CF_ERR("No reply to the lookup");
}

}  // namespace cuttlefish::nft
