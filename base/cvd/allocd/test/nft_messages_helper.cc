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

#include "allocd/test/nft_messages_helper.h"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netlink.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include <string>
#include <string_view>
#include <vector>

#include "allocd/net/netlink_request.h"

namespace cuttlefish::nft_test {
namespace {

std::vector<char> Bytes(const NetlinkRequest& req) {
  const char* data = static_cast<const char*>(req.RequestData());
  return std::vector<char>(data, data + req.RequestLength());
}

std::vector<Attr> ParseAttrs(const std::vector<char>& data) {
  std::vector<Attr> attrs;
  size_t offset = 0;
  while (offset < data.size()) {
    if (data.size() - offset < NLA_HDRLEN) {
      ADD_FAILURE() << "Truncated attribute header at offset " << offset;
      break;
    }
    nlattr header{};
    memcpy(&header, data.data() + offset, sizeof(header));
    if (header.nla_len < NLA_HDRLEN || header.nla_len > data.size() - offset) {
      ADD_FAILURE() << "Malformed attribute at offset " << offset;
      break;
    }
    attrs.push_back(Attr{
        .type = static_cast<uint16_t>(header.nla_type & NLA_TYPE_MASK),
        .payload = std::vector<char>(data.begin() + offset + NLA_HDRLEN,
                                     data.begin() + offset + header.nla_len),
    });
    offset += NLA_ALIGN(header.nla_len);
  }
  return attrs;
}

const Attr* Find(const std::vector<Attr>& attrs, uint16_t type) {
  for (const Attr& attr : attrs) {
    if (attr.type == type) {
      return &attr;
    }
  }
  return nullptr;
}

}  // namespace

std::vector<Message> ParseMessages(const std::vector<char>& data) {
  constexpr size_t kBodyOffset =
      sizeof(nlmsghdr) + NLMSG_ALIGN(sizeof(nfgenmsg));
  std::vector<Message> messages;
  size_t offset = 0;
  while (offset < data.size()) {
    if (data.size() - offset < sizeof(nlmsghdr)) {
      ADD_FAILURE() << "Truncated message header at offset " << offset;
      break;
    }
    nlmsghdr header{};
    memcpy(&header, data.data() + offset, sizeof(header));
    if (header.nlmsg_len < kBodyOffset ||
        header.nlmsg_len > data.size() - offset) {
      ADD_FAILURE() << "Malformed message at offset " << offset;
      break;
    }
    nfgenmsg nfgen{};
    memcpy(&nfgen, data.data() + offset + sizeof(header), sizeof(nfgen));
    messages.push_back(Message{
        .type = header.nlmsg_type,
        .flags = header.nlmsg_flags,
        .family = nfgen.nfgen_family,
        .res_id = ntohs(nfgen.res_id),
        .attrs = ParseAttrs(
            std::vector<char>(data.begin() + offset + kBodyOffset,
                              data.begin() + offset + header.nlmsg_len)),
    });
    offset += NLMSG_ALIGN(header.nlmsg_len);
  }
  return messages;
}

std::vector<Message> ParseMessages(const NetlinkRequest& message) {
  return ParseMessages(Bytes(message));
}

std::string GetStringAttr(const std::vector<Attr>& attrs, uint16_t type) {
  const Attr* attr = Find(attrs, type);
  if (attr == nullptr || attr->payload.empty() ||
      attr->payload.back() != '\0') {
    ADD_FAILURE() << "No NUL-terminated string attribute of type " << type;
    return "";
  }
  return std::string(attr->payload.data(), attr->payload.size() - 1);
}

uint32_t GetUint32BigEndianAttr(const std::vector<Attr>& attrs, uint16_t type) {
  const Attr* attr = Find(attrs, type);
  if (attr == nullptr || attr->payload.size() != sizeof(uint32_t)) {
    ADD_FAILURE() << "No u32 attribute of type " << type;
    return 0;
  }
  uint32_t value = 0;
  memcpy(&value, attr->payload.data(), sizeof(value));
  return ntohl(value);
}

std::vector<Attr> GetNestedAttrs(const std::vector<Attr>& attrs,
                                 uint16_t type) {
  const Attr* attr = Find(attrs, type);
  if (attr == nullptr) {
    ADD_FAILURE() << "No nested attribute of type " << type;
    return {};
  }
  return ParseAttrs(attr->payload);
}

std::string GetIpv4AddressAttr(const std::vector<Attr>& attrs, uint16_t type) {
  const std::vector<Attr> data = GetNestedAttrs(attrs, type);
  const Attr* value = Find(data, NFTA_DATA_VALUE);
  if (value == nullptr || value->payload.size() != sizeof(in_addr)) {
    ADD_FAILURE() << "No IPv4 data value in attribute of type " << type;
    return "";
  }
  in_addr address{};
  memcpy(&address, value->payload.data(), sizeof(address));
  char text[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &address, text, sizeof(text)) == nullptr) {
    ADD_FAILURE() << "inet_ntop failed";
    return "";
  }
  return text;
}

std::vector<Expression> GetRuleExpressions(const Message& rule) {
  std::vector<Expression> expressions;
  for (const Attr& element :
       GetNestedAttrs(rule.attrs, NFTA_RULE_EXPRESSIONS)) {
    if (element.type != NFTA_LIST_ELEM) {
      continue;
    }
    const std::vector<Attr> expression = ParseAttrs(element.payload);
    expressions.push_back(Expression{
        .name = GetStringAttr(expression, NFTA_EXPR_NAME),
        .attrs = GetNestedAttrs(expression, NFTA_EXPR_DATA),
    });
  }
  return expressions;
}

uint16_t NftType(uint16_t message) {
  return (NFNL_SUBSYS_NFTABLES << 8) | message;
}

std::vector<char> BuildErrorReply(int error) {
  NetlinkRequest req(NLMSG_ERROR, 0);
  req.Append(nlmsgerr{.error = -error, .msg = {}});
  return Bytes(req);
}

std::vector<char> BuildTableReply(std::string_view name) {
  NetlinkRequest req(NftType(NFT_MSG_NEWTABLE), 0);
  req.Append(nfgenmsg{
      .nfgen_family = NFPROTO_IPV4,
      .version = NFNETLINK_V0,
      .res_id = 0,
  });
  req.AddString(NFTA_TABLE_NAME, std::string(name));
  return Bytes(req);
}

std::vector<char> Concat(std::vector<char> a, const std::vector<char>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

}  // namespace cuttlefish::nft_test
