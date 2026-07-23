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

#ifndef ALLOCD_NET_NFTABLES_NFT_H_
#define ALLOCD_NET_NFTABLES_NFT_H_

#include <stdint.h>

#include <string_view>

#include "allocd/net/nftables.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class NftablesNft : public Nftables {
 public:
  NftablesNft() = default;
  ~NftablesNft() override = default;

  // Returns the resolved path to the `nft` binary, or an error if it is not
  // available. Static so callers can probe for nft support without having to
  // construct an instance.
  static Result<std::string> BinaryPath();

  Result<void> EnsureTable(std::string_view family,
                           std::string_view table) override;
  Result<void> DeleteTable(std::string_view family,
                           std::string_view table) override;
  Result<void> EnsureChain(std::string_view family, std::string_view table,
                           std::string_view chain,
                           std::string_view content) override;
  Result<uint32_t> AddRule(std::string_view family, std::string_view table,
                           std::string_view chain,
                           std::string_view content) override;
  Result<void> DeleteRule(std::string_view family, std::string_view table,
                          std::string_view chain, uint32_t handle) override;
};

}  // namespace cuttlefish

#endif  // ALLOCD_NET_NFTABLES_NFT_H_
