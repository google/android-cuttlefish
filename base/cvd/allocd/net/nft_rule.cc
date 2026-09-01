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

#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

#include "allocd/net/nftables.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

// Namespace prefix applied to every dynamic cvdalloc rule comment.
constexpr std::string_view kCvdallocCommentPrefix = "cvdalloc-";

}  // namespace

Result<NftRule> NftRule::Create(Nftables& nft, std::string_view family,
                                std::string_view table, std::string_view chain,
                                std::string_view content,
                                std::string_view tag) {
  std::string comment = absl::StrCat(kCvdallocCommentPrefix, tag);
  std::string full_content =
      absl::StrCat(content, " comment \"", comment, "\"");
  CF_EXPECT(nft.AddRule(family, table, chain, full_content));
  return NftRule(&nft, family, table, chain, std::move(comment));
}

NftRule::NftRule(Nftables* nft, std::string_view family, std::string_view table,
                 std::string_view chain, std::string comment)
    : nft_(nft),
      family_(family),
      table_(table),
      chain_(chain),
      comment_(std::move(comment)) {}

NftRule::NftRule(NftRule&& r) noexcept
    : nft_(std::exchange(r.nft_, nullptr)),
      family_(std::move(r.family_)),
      table_(std::move(r.table_)),
      chain_(std::move(r.chain_)),
      comment_(std::move(r.comment_)) {}

NftRule::~NftRule() {
  // nft_ is nulled on move, so a moved-from rule is inert and never deletes.
  if (nft_ != nullptr) {
    auto res = nft_->DeleteRulesByComment(family_, table_, chain_, comment_);
    if (!res.has_value()) {
      LOG(ERROR) << "Failed to delete nft rule(s) in NftRule destructor: "
                 << res.error();
    }
  }
}

}  // namespace cuttlefish
