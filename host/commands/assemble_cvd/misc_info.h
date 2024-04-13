//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "common/libs/utils/result.h"

namespace cuttlefish {

// TODO(chadreynolds): rename MiscInfo to more generic KeyValueFile since this
// logic is processing multiple filetypes now
using MiscInfo = std::map<std::string, std::string>;

Result<MiscInfo> ParseMiscInfo(const std::string& file_contents);
std::string WriteMiscInfo(const MiscInfo& info);
Result<MiscInfo> GetCombinedDynamicPartitions(const MiscInfo& vendor_info,
                                              const MiscInfo& system_info);
void MergeInKeys(const MiscInfo& source, MiscInfo& target);

} // namespace cuttlefish
