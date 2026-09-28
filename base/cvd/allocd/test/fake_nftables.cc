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

#include "allocd/test/fake_nftables.h"

#include <stdint.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

const FakeNftables::Table* FakeNftables::FindTable(
    std::string_view family, std::string_view table) const {
  auto it = tables_.find({std::string(family), std::string(table)});
  return it == tables_.end() ? nullptr : &it->second;
}

FakeNftables::Table* FakeNftables::FindTable(std::string_view family,
                                             std::string_view table) {
  auto it = tables_.find({std::string(family), std::string(table)});
  return it == tables_.end() ? nullptr : &it->second;
}

Result<void> FakeNftables::EnsureTable(std::string_view family,
                                       std::string_view table) {
  tables_.try_emplace({std::string(family), std::string(table)});
  return {};
}

Result<void> FakeNftables::DeleteTable(std::string_view family,
                                       std::string_view table) {
  auto it = tables_.find({std::string(family), std::string(table)});
  CF_EXPECTF(it != tables_.end(), "no such table: family={}, table={}", family,
             table);
  // Erasing the table drops its chains/rules; a later EnsureTable starts a
  // fresh handle counter, exactly as nft does when a table is recreated.
  tables_.erase(it);
  return {};
}

Result<void> FakeNftables::EnsureChain(std::string_view family,
                                       std::string_view table,
                                       std::string_view chain,
                                       std::string_view /*content*/) {
  Table* t = FindTable(family, table);
  CF_EXPECTF(t != nullptr, "no such table: family={}, table={}", family, table);
  t->chains.try_emplace(std::string(chain));
  return {};
}

Result<uint64_t> FakeNftables::AddRule(std::string_view family,
                                       std::string_view table,
                                       std::string_view chain,
                                       std::string_view content,
                                       std::string_view comment) {
  Table* t = FindTable(family, table);
  CF_EXPECTF(t != nullptr, "no such table: family={}, table={}", family, table);
  auto chain_it = t->chains.find(chain);
  CF_EXPECTF(chain_it != t->chains.end(),
             "no such chain: family={}, table={}, chain={}", family, table,
             chain);
  uint64_t handle = t->next_handle++;
  chain_it->second.push_back(
      Rule{handle, std::string(content), std::string(comment)});
  return handle;
}

Result<void> FakeNftables::DeleteRule(std::string_view family,
                                      std::string_view table,
                                      std::string_view chain, uint64_t handle) {
  Table* t = FindTable(family, table);
  CF_EXPECTF(t != nullptr, "no such table: family={}, table={}", family, table);
  auto chain_it = t->chains.find(chain);
  CF_EXPECTF(chain_it != t->chains.end(),
             "no such chain: family={}, table={}, chain={}", family, table,
             chain);
  std::vector<Rule>::size_type removed = std::erase_if(
      chain_it->second, [&](const Rule& r) { return r.handle == handle; });
  CF_EXPECTF(removed > 0, "no rule with handle {}", handle);
  return {};
}

Result<void> FakeNftables::DeleteRulesByComment(std::string_view family,
                                                std::string_view table,
                                                std::string_view chain,
                                                std::string_view comment) {
  // A missing table/chain or a comment that matches nothing is a no-op,
  // matching the idempotent teardown semantics of the real implementation.
  Table* t = FindTable(family, table);
  if (t == nullptr) {
    return {};
  }
  auto chain_it = t->chains.find(chain);
  if (chain_it == t->chains.end()) {
    return {};
  }
  std::erase_if(chain_it->second,
                [&](const Rule& r) { return r.comment == comment; });
  return {};
}

bool FakeNftables::HasTable(std::string_view family,
                            std::string_view table) const {
  return FindTable(family, table) != nullptr;
}

bool FakeNftables::HasChain(std::string_view family, std::string_view table,
                            std::string_view chain) const {
  const Table* t = FindTable(family, table);
  return t != nullptr && t->chains.contains(chain);
}

bool FakeNftables::HasRuleWithComment(std::string_view family,
                                      std::string_view table,
                                      std::string_view chain,
                                      std::string_view comment) const {
  const Table* t = FindTable(family, table);
  if (t == nullptr) {
    return false;
  }
  auto chain_it = t->chains.find(chain);
  if (chain_it == t->chains.end()) {
    return false;
  }
  return std::any_of(chain_it->second.begin(), chain_it->second.end(),
                     [&](const Rule& r) { return r.comment == comment; });
}

int FakeNftables::RuleCount(std::string_view family, std::string_view table,
                            std::string_view chain) const {
  const Table* t = FindTable(family, table);
  if (t == nullptr) {
    return 0;
  }
  auto chain_it = t->chains.find(chain);
  if (chain_it == t->chains.end()) {
    return 0;
  }
  return static_cast<int>(chain_it->second.size());
}

}  // namespace cuttlefish
