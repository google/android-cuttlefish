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

#pragma once

#include <stdint.h>

#include <vector>

#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/host/commands/cvd/cli/selector/selector_common_parser.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace selector {

// Extracts the number of instances to create and the optionally requested
// instance ids from a number of flags and selector options.
class NumInstancesParser {
 public:
  std::vector<Flag> Flags(const selector::SelectorOptions&);

  // These will only return the default values until the command line arguments
  // are parsed by the flags returned from the Flags function.
  size_t NumInstances() const;
  std::vector<uint32_t> InstanceIds() const;

 private:
  Result<void> Validate() const;

  std::optional<size_t> num_instance_names_;
  std::optional<size_t> num_instances_;
  std::optional<unsigned> base_instance_num_;
  std::optional<std::vector<unsigned>> instance_nums_;
};
}  // namespace selector
}  // namespace cuttlefish
