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

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "allocd/net/nftables.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// A minimal, stateful in-memory Nftables test double.
//
// It models a single (family, table, chain) and ignores those arguments. 
// Its purpose is to reflect the one behaviour that we need statefulness for
// (handle numbers).
class FakeNftables : public Nftables {
 public:
  Result<void> EnsureTable(std::string_view /*family*/,
                           std::string_view /*table*/) override {
    return {};
  }

  // Deleting the table drops its rules and restarts the handle counter, exactly
  // as nft does when the table is later recreated.
  Result<void> DeleteTable(std::string_view /*family*/,
                           std::string_view /*table*/) override {
    rules_.clear();
    next_handle_ = 1;
    return {};
  }

  Result<void> EnsureChain(std::string_view /*family*/,
                           std::string_view /*table*/,
                           std::string_view /*chain*/,
                           std::string_view /*content*/) override {
    return {};
  }

  Result<uint64_t> AddRule(std::string_view /*family*/,
                           std::string_view /*table*/,
                           std::string_view /*chain*/,
                           std::string_view content) override {
    uint64_t handle = next_handle_++;
    rules_.push_back(Rule{handle, ParseComment(content)});
    return handle;
  }

  Result<void> DeleteRule(std::string_view /*family*/,
                          std::string_view /*table*/,
                          std::string_view /*chain*/,
                          uint64_t handle) override {
    auto it = std::remove_if(rules_.begin(), rules_.end(),
                             [&](const Rule& r) { return r.handle == handle; });
    CF_EXPECTF(it != rules_.end(), "no rule with handle {}", handle);
    rules_.erase(it, rules_.end());
    return {};
  }

  Result<void> DeleteRulesByComment(std::string_view /*family*/,
                                    std::string_view /*table*/,
                                    std::string_view /*chain*/,
                                    std::string_view comment) override {
    // A missing match is a no-op, matching idempotent teardown semantics.
    rules_.erase(
        std::remove_if(rules_.begin(), rules_.end(),
                       [&](const Rule& r) { return r.comment == comment; }),
        rules_.end());
    return {};
  }

  bool HasRuleWithComment(std::string_view comment) const {
    return std::any_of(rules_.begin(), rules_.end(),
                       [&](const Rule& r) { return r.comment == comment; });
  }

 private:
  struct Rule {
    uint64_t handle;
    std::string comment;
  };

  // Extracts the value of an nft `comment "..."` token, if present.
  static std::string ParseComment(std::string_view content) {
    constexpr std::string_view kMarker = "comment \"";
    auto pos = content.find(kMarker);
    if (pos == std::string_view::npos) {
      return "";
    }
    auto start = pos + kMarker.size();
    auto end = content.find('"', start);
    if (end == std::string_view::npos) {
      return "";
    }
    return std::string(content.substr(start, end - start));
  }

  std::vector<Rule> rules_;
  uint64_t next_handle_ = 1;
};

}  // namespace cuttlefish

#endif  // ALLOCD_TEST_FAKE_NFTABLES_H_
