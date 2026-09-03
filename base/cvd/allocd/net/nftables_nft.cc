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

#include "allocd/net/nftables_nft.h"

#include <stdint.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "json/value.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/managed_stdio.h"
#include "cuttlefish/process/subprocess.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

constexpr std::string_view kNftBinary = "nft";

// nft usually lives in an sbin directory, which is often not on PATH for
// non-root callers, so fall back to the usual sbin locations rather than
// bailing out if we don't find it at the PATH.
Result<std::string> SearchForNft() {
  Result<std::string> p = Search(Path(), std::string(kNftBinary));
  if (p.has_value()) {
    return p;
  }
  return CF_EXPECT(Search({"/usr/sbin", "/sbin"}, std::string(kNftBinary)),
                   "could not find nft binary");
}

}  // namespace

Result<std::string> NftablesNft::BinaryPath() {
  static const absl::NoDestructor<std::string> path(
      SearchForNft().value_or(""));
  CF_EXPECT(!path->empty(), "could not find nft binary");
  return *path;
}

Result<void> NftablesNft::EnsureTable(std::string_view family,
                                      std::string_view table) {
  Command cmd = Command(CF_EXPECT(BinaryPath()))
                    .AddParameter("add")
                    .AddParameter("table")
                    .AddParameter(family)
                    .AddParameter(table);

  CF_EXPECTF(cmd.Start().Wait() == 0,
             "Failed to ensure nft table: family={}, table={}", family, table);
  return {};
}

Result<void> NftablesNft::DeleteTable(std::string_view family,
                                      std::string_view table) {
  Command cmd = Command(CF_EXPECT(BinaryPath()))
                    .AddParameter("delete")
                    .AddParameter("table")
                    .AddParameter(family)
                    .AddParameter(table);

  CF_EXPECTF(cmd.Start().Wait() == 0,
             "Failed to delete nft table: family={}, table={}", family, table);
  return {};
}

Result<void> NftablesNft::EnsureChain(std::string_view family,
                                      std::string_view table,
                                      std::string_view chain,
                                      std::string_view content) {
  Command cmd = Command(CF_EXPECT(BinaryPath()))
                    .AddParameter("add")
                    .AddParameter("chain")
                    .AddParameter(family)
                    .AddParameter(table)
                    .AddParameter(chain);
  if (!content.empty()) {
    cmd.AddParameter(content);
  }

  CF_EXPECTF(
      cmd.Start().Wait() == 0,
      "Failed to ensure nft chain: family={}, table={}, chain={}, content={}",
      family, table, chain, content);
  return {};
}

Result<uint64_t> NftablesNft::AddRule(std::string_view family,
                                      std::string_view table,
                                      std::string_view chain,
                                      std::string_view content,
                                      std::string_view comment) {
  std::string rule = comment.empty()
                         ? std::string(content)
                         : absl::StrCat(content, " comment \"", comment, "\"");

  Command cmd = Command(CF_EXPECT(BinaryPath()))
                    .AddParameter("-j")
                    .AddParameter("-e")
                    .AddParameter("add")
                    .AddParameter("rule")
                    .AddParameter(family)
                    .AddParameter(table)
                    .AddParameter(chain)
                    .AddParameter(rule);

  std::string stdout_str = CF_EXPECT(RunAndCaptureStdout(std::move(cmd)));
  Json::Value json = CF_EXPECT(ParseJson(stdout_str));

  CF_EXPECTF(json.isMember("nftables") && json["nftables"].isArray(),
             "Invalid JSON output from nft: {}", stdout_str);

  for (const auto& item : json["nftables"]) {
    if (item.isMember("add") && item["add"].isMember("rule") &&
        item["add"]["rule"].isMember("handle")) {
      return item["add"]["rule"]["handle"].asUInt64();
    }
  }

  return CF_ERRF("No rule handle found in nft JSON output: {}", stdout_str);
}

Result<void> NftablesNft::DeleteRule(std::string_view family,
                                     std::string_view table,
                                     std::string_view chain, uint64_t handle) {
  Command cmd = Command(CF_EXPECT(BinaryPath()))
                    .AddParameter("delete")
                    .AddParameter("rule")
                    .AddParameter(family)
                    .AddParameter(table)
                    .AddParameter(chain)
                    .AddParameter("handle")
                    .AddParameter(std::to_string(handle));

  CF_EXPECTF(cmd.Start().Wait() == 0,
             "Failed to delete nft rule: family={}, table={}, chain={}, "
             "handle={}",
             family, table, chain, handle);
  return {};
}

Result<void> NftablesNft::DeleteRulesByComment(std::string_view family,
                                               std::string_view table,
                                               std::string_view chain,
                                               std::string_view comment) {
  Command cmd = Command(CF_EXPECT(BinaryPath()))
                    .AddParameter("-j")
                    .AddParameter("list")
                    .AddParameter("chain")
                    .AddParameter(family)
                    .AddParameter(table)
                    .AddParameter(chain);

  // If the chain/table no longer exists (e.g. torn down already), there is
  // nothing to delete; treat that as success so teardown stays idempotent.
  Result<std::string> stdout_str = RunAndCaptureStdout(std::move(cmd));
  if (!stdout_str.has_value()) {
    LOG(INFO) << "nft list chain failed, treating as no-op: family=" << family
              << ", table=" << table << ", chain=" << chain;
    return {};
  }

  Json::Value json = CF_EXPECT(ParseJson(*stdout_str));
  CF_EXPECTF(json.isMember("nftables") && json["nftables"].isArray(),
             "Invalid JSON output from nft: {}", *stdout_str);

  std::vector<uint64_t> handles;
  for (const auto& item : json["nftables"]) {
    if (!item.isMember("rule")) {
      continue;
    }
    const Json::Value& rule = item["rule"];
    if (rule.isMember("comment") && rule.isMember("handle") &&
        rule["comment"].asString() == comment) {
      handles.push_back(rule["handle"].asUInt64());
    }
  }

  Result<void> deletion_result = {};
  for (uint64_t handle : handles) {
    Result<void> res = DeleteRule(family, table, chain, handle);
    if (!res.has_value()) {
      LOG(ERROR) << "Failed to delete nft rule, continuing: handle=" << handle
                 << ": " << res.error();
      if (deletion_result.has_value()) {
        deletion_result = std::move(res);
      }
    }
  }

  return deletion_result;
}

}  // namespace cuttlefish
