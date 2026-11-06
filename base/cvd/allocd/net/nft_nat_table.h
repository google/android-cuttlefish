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

#ifndef ALLOCD_NET_NFT_NAT_TABLE_H_
#define ALLOCD_NET_NFT_NAT_TABLE_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "allocd/net/nfnetlink_socket.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Owns an ip family nftables table that masquerades a fixed set of source
// networks.
class NftNatTable {
 public:
  // Fails if a table with this name already exists.
  static Result<NftNatTable> Create(
      std::string_view name,
      const std::vector<std::string>& masquerade_sources);
  static Result<NftNatTable> Create(
      std::unique_ptr<NfnetlinkSocket> socket, std::string_view name,
      const std::vector<std::string>& masquerade_sources);

  static Result<bool> Exists(std::string_view name);
  // For tests to pass in a fake socket.
  static Result<bool> Exists(NfnetlinkSocket& socket, std::string_view name);

  ~NftNatTable();

  NftNatTable(NftNatTable&& t) noexcept = default;
  NftNatTable& operator=(NftNatTable&& t) = delete;
  NftNatTable(const NftNatTable& t) = delete;
  NftNatTable& operator=(const NftNatTable& t) = delete;

 private:
  NftNatTable(std::unique_ptr<NfnetlinkSocket> socket, std::string_view name);

  // The kernel deletes the table when this socket closes.
  std::unique_ptr<NfnetlinkSocket> socket_;
  std::string name_;
};

}  // namespace cuttlefish

#endif  // ALLOCD_NET_NFT_NAT_TABLE_H_
