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

#include "cuttlefish/host/commands/assemble_cvd/android_build/super_image.h"

#include <functional>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "liblp/liblp.h"
#include "liblp/metadata_format.h"

#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

class SuperImageAsBuildImpl : public AndroidBuild {
 public:
  SuperImageAsBuildImpl(
      std::unique_ptr<android::fs_mgr::LpMetadata> super_metadata)
      : super_metadata_(std::move(super_metadata)) {}

  Result<std::set<std::string, std::less<void>>> LogicalPartitions() override {
    std::set<std::string, std::less<void>> ret;
    for (const LpMetadataPartition& partition : super_metadata_->partitions) {
      ret.emplace(android::fs_mgr::GetPartitionName(partition));
    }
    return ret;
  }

  Result<std::set<std::string, std::less<void>>> PartitionsInGroup(std::string_view match) {
    std::set<std::string, std::less<void>> ret;
    for (const LpMetadataPartition& partition : super_metadata_->partitions) {
      CF_EXPECT_LE(partition.group_index, super_metadata_->groups.size());
      std::string group_name = android::fs_mgr::GetPartitionGroupName(
          super_metadata_->groups[partition.group_index]);

      if (absl::StrContains(group_name, match)) {
        ret.emplace(android::fs_mgr::GetPartitionName(partition));
      }
    }
    return ret;
  }

  Result<std::set<std::string, std::less<void>>> AbPartitions() override {
    std::set<std::string, std::less<void>> ret;
    for (const LpMetadataPartition& partition : super_metadata_->partitions) {
      if (partition.attributes & LP_PARTITION_ATTR_SLOT_SUFFIXED) {
        ret.emplace(android::fs_mgr::GetPartitionName(partition));
      }
    }
    return ret;
  }

  Result<std::set<std::string, std::less<void>>> SystemPartitions() override {
    return CF_EXPECT(PartitionsInGroup("system"));
  }

  Result<std::set<std::string, std::less<void>>> VendorPartitions() override {
    return CF_EXPECT(PartitionsInGroup("vendor"));
  }
 private:
  std::ostream& Format(std::ostream& out) const override {
    return out << "MetadataFromSuperImage";
  }

  std::unique_ptr<android::fs_mgr::LpMetadata> super_metadata_;
};

Result<std::unique_ptr<android::fs_mgr::LpMetadata>> SuperImageFromAndroidBuild(
    AndroidBuild& build, std::string_view extract_dir) {
  static constexpr std::string_view kSuperEmpty = "super_empty";
  static constexpr std::string_view kSuper = "super";
  // Prefer an already extracted file, then prefer extracting super_empty since
  // it is smaller.
  Result<std::string> path;
  if (path = build.ImageFile(kSuperEmpty); path.ok()) {
  } else if (path = build.ImageFile(kSuper); path.ok()) {
  } else if (path = build.ImageFile(kSuperEmpty, extract_dir); path.ok()) {
  } else if (path = build.ImageFile(kSuper, extract_dir); path.ok()) {
  } else {
    return CF_ERR("No super.img or super_empty.img could be found");
  }
  std::unique_ptr<android::fs_mgr::LpMetadata> metadata =
      android::fs_mgr::ReadFromImageFile(*path);
  CF_EXPECTF(metadata.get(), "Failed to parse super image '{}'", *path);
  return metadata;
}

}  // namespace

Result<std::unique_ptr<AndroidBuild>> SuperImageAsBuild(
    AndroidBuild& build, std::string_view extract_dir) {
  std::unique_ptr<android::fs_mgr::LpMetadata> lp_metadata =
      CF_EXPECT(SuperImageFromAndroidBuild(build, extract_dir));

  auto super_build =
      std::make_unique<SuperImageAsBuildImpl>(std::move(lp_metadata));

  CF_EXPECT(super_build->SystemPartitions());
  CF_EXPECT(super_build->VendorPartitions());
  CF_EXPECT(super_build->LogicalPartitions());
  CF_EXPECT(super_build->AbPartitions());

  return super_build;
}

}  // namespace cuttlefish
