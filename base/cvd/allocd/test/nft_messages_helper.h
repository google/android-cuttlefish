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

#ifndef ALLOCD_TEST_NFT_MESSAGES_HELPER_H_
#define ALLOCD_TEST_NFT_MESSAGES_HELPER_H_

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "allocd/net/netlink_request.h"

// This is the test-side inverse of cuttlefish::nft.
// It decodes client request bytes and builds fake kernel reply bytes.
namespace cuttlefish::nft_test {

struct Attr {
  // With the NLA_F_* flags stripped.
  uint16_t type;
  std::vector<char> payload;
};

struct Message {
  uint16_t type;
  uint16_t flags;
  uint8_t family;
  // In host byte order.
  uint16_t res_id;
  // Attributes following the nfgenmsg.
  std::vector<Attr> attrs;
};

std::vector<Message> ParseMessages(const std::vector<char>& data);

// Decodes a single message, without batch begin and end around it.
std::vector<Message> ParseMessages(const NetlinkRequest& message);

std::string GetStringAttr(const std::vector<Attr>& attrs, uint16_t type);
uint32_t GetUint32BigEndianAttr(const std::vector<Attr>& attrs, uint16_t type);
std::vector<Attr> GetNestedAttrs(const std::vector<Attr>& attrs, uint16_t type);

// Returns the IPv4 address inside a nested NFTA_DATA_VALUE.
std::string GetIpv4AddressAttr(const std::vector<Attr>& attrs, uint16_t type);

struct Expression {
  std::string name;
  // From inside NFTA_EXPR_DATA.
  std::vector<Attr> attrs;
};
std::vector<Expression> GetRuleExpressions(const Message& rule);

uint16_t NftType(uint16_t message);

std::vector<char> BuildErrorReply(int error);

std::vector<char> BuildTableReply(std::string_view name);
std::vector<char> Concat(std::vector<char> a, const std::vector<char>& b);

}  // namespace cuttlefish::nft_test

#endif  // ALLOCD_TEST_NFT_MESSAGES_HELPER_H_
