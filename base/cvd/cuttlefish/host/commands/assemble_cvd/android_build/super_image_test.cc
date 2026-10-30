//
// Copyright (C) 2026 The Android Open Source Project
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

#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"
#include "liblp/builder.h"
#include "liblp/liblp.h"
#include "liblp/metadata_format.h"
#include "liblp/partition_opener.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/fake_android_build.h"
#include "cuttlefish/host/libs/image_aggregator/sparse_image.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

class SuperImageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string tmpl = "/tmp/cf_super_image_test_XXXXXX";
    char* created = mkdtemp(tmpl.data());
    ASSERT_NE(created, nullptr);
    temp_dir_ = created;
  }

  void TearDown() override {
    for (const std::string& path : restore_writable_paths_) {
      (void)chmod(path.c_str(), 0755);
    }
    if (!temp_dir_.empty()) {
      (void)RecursivelyRemoveDirectory(temp_dir_);
    }
  }

  Result<std::string> BuildSuperImage(
      const std::string& out_dir,
      const std::map<std::string, std::string>& system_contents,
      const std::map<std::string, std::string>& vendor_contents,
      bool sparsify) {
    static constexpr uint64_t kSuperSize = 16 * 1024 * 1024;
    static constexpr uint32_t kBlockSize = 4096;
    static constexpr uint32_t kMetadataMaxSize = 65536;
    static constexpr uint32_t kMetadataSlotCount = 2;

    CF_EXPECT(EnsureDirectoryExists(out_dir));

    android::fs_mgr::BlockDeviceInfo super_dev(
        LP_METADATA_DEFAULT_PARTITION_NAME, kSuperSize, kBlockSize, 0,
        kBlockSize);
    std::unique_ptr<android::fs_mgr::MetadataBuilder> builder =
        android::fs_mgr::MetadataBuilder::New(
            {super_dev}, LP_METADATA_DEFAULT_PARTITION_NAME, kMetadataMaxSize,
            kMetadataSlotCount);
    CF_EXPECT(builder.get() != nullptr);

    CF_EXPECT(builder->AddGroup("google_system_dynamic_partitions_a", 0));
    CF_EXPECT(builder->AddGroup("google_vendor_dynamic_partitions_a", 0));

    std::string stage_dir = absl::StrCat(temp_dir_, "/stage");
    CF_EXPECT(EnsureDirectoryExists(stage_dir));

    std::map<std::string, std::string> image_files;
    auto add_partitions =
        [&](const std::map<std::string, std::string>& contents,
            const std::string& group) -> Result<void> {
      for (const auto& [name, data] : contents) {
        CF_EXPECT_EQ(data.size() % LP_SECTOR_SIZE, 0);
        std::string part_name = absl::StrCat(name, "_a");
        std::string part_file = absl::StrCat(stage_dir, "/", part_name, ".img");
        CF_EXPECT(WriteNewFile(part_file, data));
        android::fs_mgr::Partition* part = CF_EXPECT(builder->AddPartition(
            part_name, group, LP_PARTITION_ATTR_READONLY));
        CF_EXPECT(builder->ResizePartition(part, data.size()));
        image_files.emplace(part_name, part_file);
      }
      return {};
    };

    CF_EXPECT(
        add_partitions(system_contents, "google_system_dynamic_partitions_a"));
    CF_EXPECT(
        add_partitions(vendor_contents, "google_vendor_dynamic_partitions_a"));

    std::unique_ptr<android::fs_mgr::LpMetadata> metadata = builder->Export();
    CF_EXPECT(metadata.get() != nullptr);

    std::string super_path = absl::StrCat(out_dir, "/super.img");
    CF_EXPECT(android::fs_mgr::WriteToImageFile(
        super_path, *metadata, kBlockSize, image_files, sparsify));
    return super_path;
  }

  std::string temp_dir_;
  std::vector<std::string> restore_writable_paths_;
};

TEST_F(SuperImageTest,
       ParsesAndExtractsFromReadOnlySparseSuperWithoutMutatingSource) {
  std::string boot_data(4096, 'B');
  std::string init_boot_data(4096, 'I');
  std::string vbmeta_data(4096, 'V');
  std::string system_data(8192, 'S');
  std::string vendor_boot_data(4096, 'W');
  std::string vendor_data(8192, 'D');

  std::string ro_dir = absl::StrCat(temp_dir_, "/ro_build");
  Result<std::string> super_path =
      BuildSuperImage(ro_dir,
                      {
                          {"boot", boot_data},
                          {"init_boot", init_boot_data},
                          {"vbmeta", vbmeta_data},
                          {"system", system_data},
                      },
                      {
                          {"vendor_boot", vendor_boot_data},
                          {"vendor", vendor_data},
                      },
                      /* sparsify= */ true);
  ASSERT_THAT(super_path, IsOk());
  ASSERT_THAT(IsSparseImage(*super_path), IsOkAndValue(true));

  // Lock down both super.img and its containing directory as read-only.
  ASSERT_EQ(chmod(super_path->c_str(), 0444), 0);
  ASSERT_EQ(chmod(ro_dir.c_str(), 0555), 0);
  restore_writable_paths_.push_back(ro_dir);
  restore_writable_paths_.push_back(*super_path);

  FakeAndroidBuild underlying;
  underlying.AddExtractedImage("super", *super_path);

  Result<std::unique_ptr<AndroidBuild>> super_build =
      SuperImageAsBuild(underlying);
  ASSERT_THAT(super_build, IsOk());
  EXPECT_THAT(IsSparseImage(*super_path), IsOkAndValue(true));

  std::set<std::string, std::less<void>> expected_logical = {
      "boot", "init_boot", "system", "vbmeta", "vendor", "vendor_boot",
  };
  EXPECT_THAT((*super_build)->LogicalPartitions(),
              IsOkAndValue(expected_logical));
  EXPECT_THAT((*super_build)->AbPartitions(), IsOkAndValue(expected_logical));

  std::set<std::string, std::less<void>> expected_system = {
      "boot",
      "init_boot",
      "system",
      "vbmeta",
  };
  EXPECT_THAT((*super_build)->SystemPartitions(),
              IsOkAndValue(expected_system));

  std::set<std::string, std::less<void>> expected_vendor = {
      "vendor",
      "vendor_boot",
  };
  EXPECT_THAT((*super_build)->VendorPartitions(),
              IsOkAndValue(expected_vendor));

  // Verify nested extract_dir is created automatically on extraction.
  std::string extract_dir = absl::StrCat(temp_dir_, "/nested/extracted_images");
  ASSERT_THAT((*super_build)->SetExtractDir(extract_dir), IsOk());

  Result<std::string> boot_extracted = (*super_build)->ImageFile("boot", true);
  ASSERT_THAT(boot_extracted, IsOk());
  EXPECT_EQ(ReadFile(*boot_extracted), boot_data);

  Result<std::string> vendor_boot_extracted =
      (*super_build)->ImageFile("vendor_boot", true);
  ASSERT_THAT(vendor_boot_extracted, IsOk());
  EXPECT_EQ(ReadFile(*vendor_boot_extracted), vendor_boot_data);

  Result<std::string> vbmeta_extracted =
      (*super_build)->ImageFile("vbmeta", true);
  ASSERT_THAT(vbmeta_extracted, IsOk());
  EXPECT_EQ(ReadFile(*vbmeta_extracted), vbmeta_data);
  EXPECT_THAT(IsSparseImage(*super_path), IsOkAndValue(true));

  // If a cached extracted image is deleted on disk, ImageFile(..., false) fails
  // and ImageFile(..., true) re-extracts it.
  ASSERT_EQ(unlink(boot_extracted->c_str()), 0);
  EXPECT_THAT((*super_build)->ImageFile("boot", false), IsError());
  Result<std::string> boot_reextracted =
      (*super_build)->ImageFile("boot", true);
  ASSERT_THAT(boot_reextracted, IsOk());
  EXPECT_EQ(ReadFile(*boot_reextracted), boot_data);
}

TEST_F(SuperImageTest,
       ExtractsZeroExtentAndInvalidatesOrReusesAcrossExtractDirs) {
  std::string head_data(4096, 'A');
  std::string middle_noise(4096, 'X');
  std::string tail_data(4096, 'Z');

  std::string raw_dir = absl::StrCat(temp_dir_, "/raw_build");
  Result<std::string> super_path = BuildSuperImage(raw_dir,
                                                   {
                                                       {"head", head_data},
                                                       {"noise", middle_noise},
                                                       {"tail", tail_data},
                                                   },
                                                   {},
                                                   /* sparsify= */ false);
  ASSERT_THAT(super_path, IsOk());

  std::unique_ptr<android::fs_mgr::LpMetadata> orig_meta =
      android::fs_mgr::ReadMetadata(*super_path, 0);
  ASSERT_NE(orig_meta, nullptr);

  std::unique_ptr<android::fs_mgr::MetadataBuilder> builder =
      android::fs_mgr::MetadataBuilder::New(*orig_meta);
  ASSERT_NE(builder, nullptr);

  android::fs_mgr::Partition* head_part = builder->FindPartition("head_a");
  android::fs_mgr::Partition* tail_part = builder->FindPartition("tail_a");
  ASSERT_NE(head_part, nullptr);
  ASSERT_NE(tail_part, nullptr);
  ASSERT_EQ(head_part->extents().size(), 1u);
  ASSERT_EQ(tail_part->extents().size(), 1u);
  uint64_t head_sector =
      head_part->extents()[0]->AsLinearExtent()->physical_sector();
  uint64_t tail_sector =
      tail_part->extents()[0]->AsLinearExtent()->physical_sector();
  builder->RemovePartition("head_a");
  builder->RemovePartition("tail_a");

  android::fs_mgr::Partition* multi =
      builder->AddPartition("multi_a", "google_system_dynamic_partitions_a",
                            LP_PARTITION_ATTR_READONLY);
  ASSERT_NE(multi, nullptr);
  ASSERT_TRUE(
      builder->AddLinearExtent(multi, LP_METADATA_DEFAULT_PARTITION_NAME,
                               head_data.size() / LP_SECTOR_SIZE, head_sector));
  multi->AddExtent(
      std::make_unique<android::fs_mgr::ZeroExtent>(4096 / LP_SECTOR_SIZE));
  ASSERT_TRUE(
      builder->AddLinearExtent(multi, LP_METADATA_DEFAULT_PARTITION_NAME,
                               tail_data.size() / LP_SECTOR_SIZE, tail_sector));

  std::unique_ptr<android::fs_mgr::LpMetadata> updated_meta = builder->Export();
  ASSERT_NE(updated_meta, nullptr);
  ASSERT_TRUE(
      android::fs_mgr::UpdatePartitionTable(*super_path, *updated_meta, 0));
  ASSERT_TRUE(
      android::fs_mgr::UpdatePartitionTable(*super_path, *updated_meta, 1));

  FakeAndroidBuild underlying;
  underlying.AddExtractedImage("super", *super_path);

  Result<std::unique_ptr<AndroidBuild>> super_build =
      SuperImageAsBuild(underlying);
  ASSERT_THAT(super_build, IsOk());

  std::string dir1 = absl::StrCat(temp_dir_, "/extract1");
  std::string dir2 = absl::StrCat(temp_dir_, "/extract2");
  ASSERT_THAT((*super_build)->SetExtractDir(dir1), IsOk());

  Result<std::string> multi_1 = (*super_build)->ImageFile("multi", true);
  ASSERT_THAT(multi_1, IsOk());
  std::string expected_multi =
      absl::StrCat(head_data, std::string(4096, '\0'), tail_data);
  EXPECT_EQ(ReadFile(*multi_1), expected_multi);

  // Switching SetExtractDir invalidates in-memory cached paths from dir1.
  ASSERT_THAT((*super_build)->SetExtractDir(dir2), IsOk());
  EXPECT_THAT((*super_build)->ImageFile("multi", false), IsError());
  Result<std::string> multi_2 = (*super_build)->ImageFile("multi", true);
  ASSERT_THAT(multi_2, IsOk());
  EXPECT_NE(*multi_1, *multi_2);
  EXPECT_EQ(ReadFile(*multi_2), expected_multi);

  // A fresh SuperImageAsBuild pointed at dir2 reuses multi_2 without rewriting.
  struct stat st_before{};
  ASSERT_EQ(stat(multi_2->c_str(), &st_before), 0);

  Result<std::unique_ptr<AndroidBuild>> resumed_build =
      SuperImageAsBuild(underlying);
  ASSERT_THAT(resumed_build, IsOk());
  ASSERT_THAT((*resumed_build)->SetExtractDir(dir2), IsOk());
  Result<std::string> multi_resumed =
      (*resumed_build)->ImageFile("multi", false);
  ASSERT_THAT(multi_resumed, IsOkAndValue(*multi_2));

  struct stat st_after{};
  ASSERT_EQ(stat(multi_2->c_str(), &st_after), 0);
  EXPECT_EQ(st_before.st_ino, st_after.st_ino);
  EXPECT_EQ(st_before.st_mtim.tv_sec, st_after.st_mtim.tv_sec);
  EXPECT_EQ(st_before.st_mtim.tv_nsec, st_after.st_mtim.tv_nsec);
}

TEST_F(SuperImageTest, ExtractsSlotAWhenSlotBPrecedesSlotAInMetadata) {
  std::string boot_a_data(4096, 'A');
  std::string raw_dir = absl::StrCat(temp_dir_, "/slot_order_build");
  Result<std::string> super_path = BuildSuperImage(
      raw_dir, {{"boot", boot_a_data}}, {}, /* sparsify= */ false);
  ASSERT_THAT(super_path, IsOk());

  // Reorder metadata so empty boot_b appears before populated boot_a, matching
  // MetadataBuilder::RemovePartition("boot_a") + AddPartition("boot_a", ...).
  std::unique_ptr<android::fs_mgr::LpMetadata> orig_meta =
      android::fs_mgr::ReadMetadata(*super_path, 0);
  ASSERT_NE(orig_meta, nullptr);

  std::unique_ptr<android::fs_mgr::MetadataBuilder> builder =
      android::fs_mgr::MetadataBuilder::New(*orig_meta);
  ASSERT_NE(builder, nullptr);
  ASSERT_TRUE(builder->AddGroup("google_system_dynamic_partitions_b", 0));

  android::fs_mgr::Partition* old_boot_a = builder->FindPartition("boot_a");
  ASSERT_NE(old_boot_a, nullptr);
  ASSERT_EQ(old_boot_a->extents().size(), 1u);
  const uint64_t boot_a_sector =
      old_boot_a->extents()[0]->AsLinearExtent()->physical_sector();
  builder->RemovePartition("boot_a");

  ASSERT_NE(
      builder->AddPartition("boot_b", "google_system_dynamic_partitions_b",
                            LP_PARTITION_ATTR_READONLY),
      nullptr);
  android::fs_mgr::Partition* new_boot_a =
      builder->AddPartition("boot_a", "google_system_dynamic_partitions_a",
                            LP_PARTITION_ATTR_READONLY);
  ASSERT_NE(new_boot_a, nullptr);
  ASSERT_TRUE(builder->AddLinearExtent(
      new_boot_a, LP_METADATA_DEFAULT_PARTITION_NAME,
      boot_a_data.size() / LP_SECTOR_SIZE, boot_a_sector));

  std::unique_ptr<android::fs_mgr::LpMetadata> updated_meta = builder->Export();
  ASSERT_NE(updated_meta, nullptr);
  ASSERT_GE(updated_meta->partitions.size(), 2u);
  EXPECT_EQ(android::fs_mgr::GetPartitionName(updated_meta->partitions[0]),
            "boot_b");
  EXPECT_EQ(android::fs_mgr::GetPartitionName(updated_meta->partitions[1]),
            "boot_a");
  ASSERT_TRUE(
      android::fs_mgr::UpdatePartitionTable(*super_path, *updated_meta, 0));
  ASSERT_TRUE(
      android::fs_mgr::UpdatePartitionTable(*super_path, *updated_meta, 1));

  FakeAndroidBuild underlying;
  underlying.AddExtractedImage("super", *super_path);
  Result<std::unique_ptr<AndroidBuild>> super_build =
      SuperImageAsBuild(underlying);
  ASSERT_THAT(super_build, IsOk());

  std::string extract_dir = absl::StrCat(temp_dir_, "/extract_slot_order");
  ASSERT_THAT((*super_build)->SetExtractDir(extract_dir), IsOk());
  Result<std::string> extracted_boot = (*super_build)->ImageFile("boot", true);
  ASSERT_THAT(extracted_boot, IsOk());
  EXPECT_EQ(ReadFile(*extracted_boot), boot_a_data);
}

}  // namespace
}  // namespace cuttlefish
