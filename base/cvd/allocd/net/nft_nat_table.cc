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

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "allocd/net/netlink_request.h"
#include "allocd/net/nfnetlink_socket.h"
#include "allocd/net/nft_messages.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kChain = "postrouting";

Result<void> SendBatch(NfnetlinkSocket& socket,
                       const std::vector<NetlinkRequest>& changes) {
  const std::vector<char> replies =
      CF_EXPECT(socket.Transact(nft::Batch(changes)));
  CF_EXPECT(nft::CheckReplies(replies));
  return {};
}

}  // namespace

Result<NftNatTable> NftNatTable::Create(
    std::string_view name, const std::vector<std::string>& masquerade_sources) {
  return Create(CF_EXPECT(NfnetlinkSocket::Open()), name, masquerade_sources);
}

Result<NftNatTable> NftNatTable::Create(
    std::unique_ptr<NfnetlinkSocket> socket, std::string_view name,
    const std::vector<std::string>& masquerade_sources) {
  std::vector<NetlinkRequest> changes;
  changes.push_back(nft::NewTable(name));
  changes.push_back(nft::NewNatChain(name, kChain));
  for (const std::string& source : masquerade_sources) {
    changes.push_back(CF_EXPECT(nft::NewMasqueradeRule(name, kChain, source)));
  }
  CF_EXPECTF(SendBatch(*socket, changes), "Failed to create nft table '{}'",
             name);
  return NftNatTable(std::move(socket), name);
}

Result<bool> NftNatTable::Exists(std::string_view name) {
  const std::unique_ptr<NfnetlinkSocket> socket =
      CF_EXPECT(NfnetlinkSocket::Open());
  return Exists(*socket, name);
}

Result<bool> NftNatTable::Exists(NfnetlinkSocket& socket,
                                 std::string_view name) {
  const std::vector<char> replies =
      CF_EXPECT(socket.Transact(nft::GetTable(name)));
  return CF_EXPECTF(nft::TableExistsFromReply(replies),
                    "Failed to look up nft table '{}'", name);
}

NftNatTable::NftNatTable(std::unique_ptr<NfnetlinkSocket> socket,
                         std::string_view name)
    : socket_(std::move(socket)), name_(name) {}

NftNatTable::~NftNatTable() {
  if (!socket_) {
    return;
  }
  std::vector<NetlinkRequest> changes;
  changes.push_back(nft::DeleteTable(name_));
  const Result<void> res = SendBatch(*socket_, changes);
  if (!res.has_value()) {
    LOG(ERROR) << "Failed to delete nft table " << name_ << ": " << res.error();
  }
}

}  // namespace cuttlefish
