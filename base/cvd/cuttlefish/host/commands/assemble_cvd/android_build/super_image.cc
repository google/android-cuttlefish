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

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "liblp/liblp.h"
#include "liblp/metadata_format.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

bool HasSlotSuffix(const std::string_view name) {
  return absl::EndsWith(name, "_a") || absl::EndsWith(name, "_b");
}

std::string WithoutSlotSuffix(std::string_view name) {
  absl::ConsumeSuffix(&name, "_a");
  absl::ConsumeSuffix(&name, "_b");
  return std::string(name);
}

Result<std::vector<LpMetadataExtent>> PartitionExtents(
    const android::fs_mgr::LpMetadata& metadata, std::string_view name) {
  for (const LpMetadataPartition& partition : metadata.partitions) {
    std::string partition_name = android::fs_mgr::GetPartitionName(partition);
    if (name != WithoutSlotSuffix(partition_name)) {
      continue;
    }
    std::vector<LpMetadataExtent> extents;
    for (uint32_t i = 0; i < partition.num_extents; i++) {
      CF_EXPECT_LE(i + partition.first_extent_index, metadata.extents.size());
      extents.emplace_back(metadata.extents[i + partition.first_extent_index]);
    }
    return extents;
  }
  return CF_ERRF("Could not find partition with name '{}'", name);
}

Result<void> ExtractPartition(SharedFD source,
                              const std::vector<LpMetadataExtent>& extents,
                              SharedFD destination) {
  CF_EXPECT_EQ(destination->LSeek(0, SEEK_SET), 0, destination->StrError());
  for (const LpMetadataExtent& extent : extents) {
    uint64_t length = extent.num_sectors * LP_SECTOR_SIZE;
    switch (extent.target_type) {
      case LP_TARGET_TYPE_LINEAR: {
        uint64_t offset = extent.target_data * LP_SECTOR_SIZE;
        CF_EXPECT_EQ(source->LSeek(offset, SEEK_SET), offset,
                     source->StrError());
        CF_EXPECT(destination->CopyFrom(*source, length));
        break;
      };
      case LP_TARGET_TYPE_ZERO: {
        CF_EXPECT_GE(destination->LSeek(length, SEEK_CUR), 0,
                     destination->StrError());
        break;
      };
      default:
        return CF_ERRF("Unknown target_type '{}'", extent.target_type);
    }
  }
  return {};
}

class SuperImageAsBuildImpl : public AndroidBuild {
 public:
  SuperImageAsBuildImpl(
      AndroidBuild& android_build,
      std::unique_ptr<android::fs_mgr::LpMetadata> super_metadata)
      : android_build_(&android_build),
        super_metadata_(std::move(super_metadata)) {}

  std::string Name() const override { return "SuperImageAsBuild"; }

  Result<std::set<std::string, std::less<void>>> Images() override {
    std::set<std::string, std::less<void>> images =
        CF_EXPECT(android_build_->Images());
    CF_EXPECT(images.count("super"), "Can't extract from super_empty");
    return CF_EXPECT(LogicalPartitions());
  }

  Result<std::string> ImageFile(std::string_view name, bool extract) override {
    if (auto it = extracted_.find(name); it != extracted_.end()) {
      return it->second;
    }
    CF_EXPECTF(!!extract, "'{}' was not already extracted", name);
    CF_EXPECT(!extract_dir_.empty(), "`SetExtractDir` was never called");

    std::string super_path =
        CF_EXPECT(android_build_->ImageFile("super", true));
    SharedFD super_fd = SharedFD::Open(super_path, O_RDONLY);
    CF_EXPECT(super_fd->IsOpen(), super_fd->StrError());

    std::string extract_path = absl::StrCat(extract_dir_, "/", name, ".img");
    unlink(extract_path.c_str());  // Ignore errors
    SharedFD extract_fd =
        SharedFD::Open(extract_path, O_RDWR | O_CREAT | O_EXCL);
    CF_EXPECTF(extract_fd->IsOpen(), "Failed to open '{}': ", extract_path,
               extract_fd->StrError());

    CF_EXPECT(super_metadata_.get());
    std::vector<LpMetadataExtent> extents =
        CF_EXPECT(PartitionExtents(*super_metadata_, name));

    CF_EXPECT(ExtractPartition(super_fd, extents, extract_fd));
    auto [it, inserted] = extracted_.emplace(name, extract_path);
    CF_EXPECT(!!inserted);

    return extract_path;
  }

  Result<void> SetExtractDir(std::string_view extract_dir) override {
    extract_dir_ = extract_dir;
    return {};
  }

  Result<std::set<std::string, std::less<void>>> LogicalPartitions() override {
    std::set<std::string, std::less<void>> ret;
    for (const LpMetadataPartition& partition : super_metadata_->partitions) {
      std::string name = android::fs_mgr::GetPartitionName(partition);
      ret.emplace(WithoutSlotSuffix(name));
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
        std::string name = android::fs_mgr::GetPartitionName(partition);
        ret.emplace(WithoutSlotSuffix(name));
      }
    }
    return ret;
  }

  Result<std::set<std::string, std::less<void>>> AbPartitions() override {
    std::set<std::string, std::less<void>> ret;
    for (const LpMetadataPartition& partition : super_metadata_->partitions) {
      std::string name = android::fs_mgr::GetPartitionName(partition);
      if (HasSlotSuffix(name)) {
        ret.emplace(WithoutSlotSuffix(name));
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
  AndroidBuild* android_build_;
  std::unique_ptr<android::fs_mgr::LpMetadata> super_metadata_;
  std::map<std::string, std::string, std::less<void>> extracted_;
  std::string extract_dir_;
};

Result<std::unique_ptr<android::fs_mgr::LpMetadata>> SuperImageFromAndroidBuild(
    AndroidBuild& build) {
  static constexpr std::string_view kSuperEmpty = "super_empty";
  static constexpr std::string_view kSuper = "super";
  // Prefer an already extracted file, then prefer extracting super_empty since
  // it is smaller.
  Result<std::string> path;
  std::unique_ptr<android::fs_mgr::LpMetadata> metadata;
  if (path = build.ImageFile(kSuperEmpty); path.ok()) {
    metadata = android::fs_mgr::ReadFromImageFile(*path);
  } else if (path = build.ImageFile(kSuper); path.ok()) {
    metadata = android::fs_mgr::ReadMetadata(*path, 0);
  } else {
    return CF_ERR("No super.img or super_empty.img could be found");
  }
  CF_EXPECTF(metadata.get(), "Failed to parse super image '{}'", *path);
  return metadata;
}

}  // namespace

Result<std::unique_ptr<AndroidBuild>> SuperImageAsBuild(AndroidBuild& build) {
  std::unique_ptr<android::fs_mgr::LpMetadata> lp_metadata =
      CF_EXPECT(SuperImageFromAndroidBuild(build));

  auto super_build =
      std::make_unique<SuperImageAsBuildImpl>(build, std::move(lp_metadata));

  CF_EXPECT(super_build->SystemPartitions());
  CF_EXPECT(super_build->VendorPartitions());
  CF_EXPECT(super_build->LogicalPartitions());
  CF_EXPECT(super_build->AbPartitions());

  return super_build;
}

}  // namespace cuttlefish
