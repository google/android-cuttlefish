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

#include "allocd/net/nft_rule.h"

#include <stdint.h>

#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "allocd/net/nftables.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<NftRule> NftRule::Create(Nftables& nft, std::string_view family,
                                std::string_view table, std::string_view chain,
                                std::string_view content) {
  uint32_t handle = CF_EXPECT(nft.AddRule(family, table, chain, content));
  return NftRule(&nft, family, table, chain, handle);
}

NftRule::NftRule(Nftables* nft, std::string_view family, std::string_view table,
                 std::string_view chain, uint32_t handle)
    : nft_(nft),
      family_(family),
      table_(table),
      chain_(chain),
      handle_(handle) {}

NftRule::NftRule(NftRule&& r) noexcept
    : nft_(std::exchange(r.nft_, nullptr)),
      family_(std::move(r.family_)),
      table_(std::move(r.table_)),
      chain_(std::move(r.chain_)),
      handle_(std::exchange(r.handle_, 0)) {}

NftRule::~NftRule() {
  if (nft_ != nullptr && handle_ != 0) {
    auto res = nft_->DeleteRule(family_, table_, chain_, handle_);
    if (!res.ok()) {
      LOG(ERROR) << "Failed to delete nft rule in NftRule destructor: "
                 << res.error();
    }
  }
}

}  // namespace cuttlefish
