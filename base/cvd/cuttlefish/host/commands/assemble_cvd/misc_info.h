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

#include <set>
#include <string>

#include "cuttlefish/common/libs/key_equals_value/key_equals_value.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/avb/avb.h"

namespace cuttlefish {

struct VbmetaArgs {
  std::string algorithm;
  std::string key_path;
  std::vector<ChainPartition> chained_partitions;
  std::vector<std::string> included_partitions;
  std::vector<std::string> extra_arguments;
};

Result<MiscInfo> GetCombinedDynamicPartitions(
    const MiscInfo& vendor_info, const MiscInfo& system_info,
    const std::set<std::string>& extracted_images);
Result<MiscInfo> MergeMiscInfos(
    const MiscInfo& vendor_info, const MiscInfo& system_info,
    const MiscInfo& combined_dp_info,
    const std::vector<std::string>& system_partitions);
Result<VbmetaArgs> GetVbmetaArgs(const MiscInfo& misc_info,
                                 const std::string& image_path);

} // namespace cuttlefish
