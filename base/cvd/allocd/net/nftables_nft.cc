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

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/managed_stdio.h"
#include "cuttlefish/process/subprocess.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

constexpr std::string_view kNftBinary = "nft";

// Searches PATH, then the usual sbin locations, for the nft binary.
Result<std::string> SearchForNft() {
  Result<std::string> p = Search(Path(), std::string(kNftBinary));
  if (p.ok()) {
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
  Command cmd{std::string(kNftBinary)};
  cmd.AddParameter("add");
  cmd.AddParameter("table");
  cmd.AddParameter(std::string(family));
  cmd.AddParameter(std::string(table));

  CF_EXPECTF(cmd.Start().Wait() == 0,
             "Failed to ensure nft table: family={}, table={}", family, table);
  return {};
}

Result<void> NftablesNft::DeleteTable(std::string_view family,
                                      std::string_view table) {
  Command cmd{std::string(kNftBinary)};
  cmd.AddParameter("delete");
  cmd.AddParameter("table");
  cmd.AddParameter(std::string(family));
  cmd.AddParameter(std::string(table));

  CF_EXPECTF(cmd.Start().Wait() == 0,
             "Failed to delete nft table: family={}, table={}", family, table);
  return {};
}

Result<void> NftablesNft::EnsureChain(std::string_view family,
                                      std::string_view table,
                                      std::string_view chain,
                                      std::string_view content) {
  Command cmd{std::string(kNftBinary)};
  cmd.AddParameter("add");
  cmd.AddParameter("chain");
  cmd.AddParameter(std::string(family));
  cmd.AddParameter(std::string(table));
  cmd.AddParameter(std::string(chain));
  if (!content.empty()) {
    cmd.AddParameter(std::string(content));
  }

  CF_EXPECTF(
      cmd.Start().Wait() == 0,
      "Failed to ensure nft chain: family={}, table={}, chain={}, content={}",
      family, table, chain, content);
  return {};
}

Result<uint32_t> NftablesNft::AddRule(std::string_view family,
                                      std::string_view table,
                                      std::string_view chain,
                                      std::string_view content) {
  Command cmd{std::string(kNftBinary)};
  cmd.AddParameter("-j");
  cmd.AddParameter("-e");
  cmd.AddParameter("add");
  cmd.AddParameter("rule");
  cmd.AddParameter(std::string(family));
  cmd.AddParameter(std::string(table));
  cmd.AddParameter(std::string(chain));
  cmd.AddParameter(std::string(content));

  std::string stdout_str = CF_EXPECT(RunAndCaptureStdout(std::move(cmd)));
  Json::Value json = CF_EXPECT(ParseJson(stdout_str));

  CF_EXPECT(json.isMember("nftables") && json["nftables"].isArray(),
            "Invalid JSON output from nft: " << stdout_str);

  for (const auto& item : json["nftables"]) {
    if (item.isMember("add") && item["add"].isMember("rule") &&
        item["add"]["rule"].isMember("handle")) {
      return item["add"]["rule"]["handle"].asUInt();
    }
  }

  return CF_ERR("No rule handle found in nft JSON output: " << stdout_str);
}

Result<void> NftablesNft::DeleteRule(std::string_view family,
                                     std::string_view table,
                                     std::string_view chain, uint32_t handle) {
  Command cmd{std::string(kNftBinary)};
  cmd.AddParameter("delete");
  cmd.AddParameter("rule");
  cmd.AddParameter(std::string(family));
  cmd.AddParameter(std::string(table));
  cmd.AddParameter(std::string(chain));
  cmd.AddParameter("handle");
  cmd.AddParameter(std::to_string(handle));

  CF_EXPECTF(cmd.Start().Wait() == 0,
             "Failed to delete nft rule: family={}, table={}, chain={}, "
             "handle={}",
             family, table, chain, handle);
  return {};
}

}  // namespace cuttlefish
