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

#ifndef ALLOCD_NET_NFTABLES_H_
#define ALLOCD_NET_NFTABLES_H_

#include <stdint.h>

#include <string_view>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

class Nftables {
 public:
  virtual ~Nftables() = default;

  virtual Result<void> EnsureTable(std::string_view family,
                                   std::string_view table) = 0;
  virtual Result<void> DeleteTable(std::string_view family,
                                   std::string_view table) = 0;
  virtual Result<void> EnsureChain(std::string_view family,
                                   std::string_view table,
                                   std::string_view chain,
                                   std::string_view content) = 0;
  virtual Result<uint32_t> AddRule(std::string_view family,
                                   std::string_view table,
                                   std::string_view chain,
                                   std::string_view content) = 0;
  virtual Result<void> DeleteRule(std::string_view family,
                                  std::string_view table,
                                  std::string_view chain,
                                  uint32_t handle) = 0;
};

}  // namespace cuttlefish

#endif  // ALLOCD_NET_NFTABLES_H_
