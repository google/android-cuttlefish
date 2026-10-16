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

#ifndef ALLOCD_NET_NFT_MESSAGES_H_
#define ALLOCD_NET_NFT_MESSAGES_H_

#include <string_view>
#include <vector>

#include "allocd/net/netlink_request.h"
#include "cuttlefish/result/result.h"

// nf_tables netlink messages for ip family tables, and the replies to them
namespace cuttlefish::nft {

// changes
// the kernel only accepts these inside a Batch
// it applies a batch all or nothing

// corresponds to create table ip <name> with flags owner
// fails if <name> already exists
// the kernel deletes the table when the socket that created it closes
NetlinkRequest NewTable(std::string_view name);
// corresponds to delete table ip <name>
NetlinkRequest DeleteTable(std::string_view name);

// corresponds to add chain ip <table> <chain>
//   with type nat hook postrouting priority srcnat
NetlinkRequest NewNatChain(std::string_view table, std::string_view chain);
// corresponds to add rule ip <table> <chain> ip saddr <source_cidr> masquerade
Result<NetlinkRequest> NewMasqueradeRule(std::string_view table,
                                         std::string_view chain,
                                         std::string_view source_cidr);

// wraps the changes in batch begin and end messages
std::vector<char> Batch(const std::vector<NetlinkRequest>& changes);
// fails if the kernel rejected the batch or didn't reply
Result<void> CheckReplies(const std::vector<char>& reply_bytes);

// lookups
// the kernel rejects these inside a batch, so they return bytes ready to send

// corresponds to list table ip <name>
std::vector<char> GetTable(std::string_view name);
// GetTable's reply -> whether the table exists
Result<bool> TableExistsFromReply(const std::vector<char>& reply_bytes);

}  // namespace cuttlefish::nft

#endif  // ALLOCD_NET_NFT_MESSAGES_H_
