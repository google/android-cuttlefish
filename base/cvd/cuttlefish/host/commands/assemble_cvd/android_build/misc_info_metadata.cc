//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/assemble_cvd/android_build/misc_info_metadata.h"

#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "fmt/ostream.h"

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class MetadataFromMiscInfo : public AndroidBuild {
 public:
  MetadataFromMiscInfo(std::map<std::string, std::string> misc_info)
      : misc_info_(std::move(misc_info)) {}

  Result<std::set<std::string, std::less<void>>> SystemPartitions() override {
    return CF_EXPECT(PartitionsMatchingGroup("system"));
  }

  Result<std::set<std::string, std::less<void>>> VendorPartitions() override {
    return CF_EXPECT(PartitionsMatchingGroup("vendor"));
  }

  Result<std::set<std::string, std::less<void>>> LogicalPartitions() override {
    std::set<std::string, std::less<void>> partitions;
    partitions.merge(CF_EXPECT(SystemPartitions()));
    partitions.merge(CF_EXPECT(VendorPartitions()));
    return partitions;
  }

 private:
  Result<std::set<std::string, std::less<void>>> PartitionsMatchingGroup(
      std::string_view match) {
    std::string groups_key = "super_partition_groups";
    auto groups_it = misc_info_.find(groups_key);
    CF_EXPECTF(groups_it != misc_info_.end(), "Could not find entry for '{}'",
               groups_key);

    std::string_view matching_group;
    for (std::string_view group :
         absl::StrSplit(groups_it->second, " ", absl::SkipEmpty())) {
      if (absl::StrContains(group, match)) {
        matching_group = group;
      }
    }

    CF_EXPECTF(!matching_group.empty(), "No '{}' group", match);

    std::string key = absl::StrCat("super_", matching_group, "_partition_list");

    auto group_it = misc_info_.find(key);
    CF_EXPECTF(group_it != misc_info_.end(), "Could not find entry for '{}'",
               key);

    return absl::StrSplit(group_it->second, " ", absl::SkipEmpty());
  }

  std::ostream& Format(std::ostream& out) const override {
    out << "MetadataFromMiscInfo { ";
    for (auto& [key, value] : misc_info_) {
      fmt::print(out, "'{}' => '{}', ", key, value);
    }
    return out << "}";
  }

  std::map<std::string, std::string> misc_info_;
};

}  // namespace

Result<std::unique_ptr<AndroidBuild>> AndroidBuildFromMiscInfo(
    std::map<std::string, std::string> misc_info) {
  auto build = std::make_unique<MetadataFromMiscInfo>(std::move(misc_info));

  std::set<std::string, std::less<void>> system =
      CF_EXPECT(build->SystemPartitions());
  CF_EXPECT(!system.empty());

  std::set<std::string, std::less<void>> vendor =
      CF_EXPECT(build->VendorPartitions());
  CF_EXPECT(!vendor.empty());

  return build;
}

}  // namespace cuttlefish
