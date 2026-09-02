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

#ifndef ALLOCD_TEST_FAKE_NFTABLES_H_
#define ALLOCD_TEST_FAKE_NFTABLES_H_

#include <stdint.h>

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "allocd/net/nftables.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// A stateful, in-memory Nftables test double.
// Statefulness is needed for testing behaviour around handles.
class FakeNftables : public Nftables {
 public:
  Result<void> EnsureTable(std::string_view family,
                           std::string_view table) override;
  Result<void> DeleteTable(std::string_view family,
                           std::string_view table) override;
  Result<void> EnsureChain(std::string_view family, std::string_view table,
                           std::string_view chain,
                           std::string_view content) override;
  Result<uint64_t> AddRule(std::string_view family, std::string_view table,
                           std::string_view chain, std::string_view content,
                           std::string_view comment) override;
  Result<void> DeleteRule(std::string_view family, std::string_view table,
                          std::string_view chain, uint64_t handle) override;
  Result<void> DeleteRulesByComment(std::string_view family,
                                    std::string_view table,
                                    std::string_view chain,
                                    std::string_view comment) override;

  bool HasTable(std::string_view family, std::string_view table) const;
  bool HasChain(std::string_view family, std::string_view table,
                std::string_view chain) const;
  bool HasRuleWithComment(std::string_view family, std::string_view table,
                          std::string_view chain,
                          std::string_view comment) const;
  int RuleCount(std::string_view family, std::string_view table,
                std::string_view chain) const;

 private:
  struct Rule {
    uint64_t handle;
    std::string content;
    std::string comment;
  };
  struct Table {
    std::map<std::string, std::vector<Rule>> chains;
    uint64_t next_handle = 1;
  };
  using TableKey = std::pair<std::string, std::string>;

  const Table* FindTable(std::string_view family, std::string_view table) const;
  Table* FindTable(std::string_view family, std::string_view table);

  std::map<TableKey, Table> tables_;
};

}  // namespace cuttlefish

#endif  // ALLOCD_TEST_FAKE_NFTABLES_H_
