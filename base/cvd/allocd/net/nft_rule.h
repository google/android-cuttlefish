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

#ifndef ALLOCD_NET_NFT_RULE_H_
#define ALLOCD_NET_NFT_RULE_H_

#include <stdint.h>

#include <string>
#include <string_view>

#include "allocd/net/nftables.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

class NftRule {
 public:
  static Result<NftRule> Create(Nftables& nft, std::string_view family,
                                std::string_view table, std::string_view chain,
                                std::string_view content);

  NftRule() = delete;
  NftRule(Nftables* nft, std::string_view family, std::string_view table,
          std::string_view chain, uint32_t handle);
  ~NftRule();

  NftRule(NftRule&& r) noexcept;
  NftRule& operator=(NftRule&& r) = delete;
  NftRule(const NftRule& r) = delete;
  NftRule& operator=(const NftRule& r) = delete;

 private:
  Nftables* nft_ = nullptr;
  std::string family_;
  std::string table_;
  std::string chain_;
  uint32_t handle_ = 0;
};

}  // namespace cuttlefish

#endif  // ALLOCD_NET_NFT_RULE_H_
