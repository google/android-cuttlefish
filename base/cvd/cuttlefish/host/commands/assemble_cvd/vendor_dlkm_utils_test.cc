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

#include "cuttlefish/host/commands/assemble_cvd/vendor_dlkm_utils.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <map>
#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "android-base/unique_fd.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"
#include "liblp/builder.h"
#include "liblp/liblp.h"
#include "liblp/metadata_format.h"
#include "liblp/partition_opener.h"
#include "sparse/sparse.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/fake_android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/super_image.h"
#include "cuttlefish/host/libs/image_aggregator/sparse_image.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

class VendorDlkmUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string tmpl = "/tmp/cf_vendor_dlkm_utils_test_XXXXXX";
    char* created = mkdtemp(tmpl.data());
    ASSERT_NE(created, nullptr);
    temp_dir_ = created;
  }

  void TearDown() override {
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
        CF_EXPECT_EQ(data.size() % LP_SECTOR_SIZE, 0u);
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
};

TEST_F(VendorDlkmUtilsTest,
       RepacksSparseSuperPreservesGroupPadsTailAndIsIdempotent) {
  std::string orig_boot(4096, 'B');
  std::string orig_vendor(8192, 'V');

  Result<std::string> super_path =
      BuildSuperImage(absl::StrCat(temp_dir_, "/inst"), {{"boot", orig_boot}},
                      {{"vendor", orig_vendor}},
                      /* sparsify= */ true);
  ASSERT_THAT(super_path, IsOk());
  ASSERT_THAT(IsSparseImage(*super_path), IsOkAndValue(true));

  EXPECT_THAT(SuperHasPartition(*super_path, "boot"), IsOkAndValue(true));
  EXPECT_THAT(SuperHasPartition(*super_path, "vendor"), IsOkAndValue(true));
  EXPECT_THAT(SuperHasPartition(*super_path, "vbmeta_vendor_dlkm"),
              IsOkAndValue(false));
  EXPECT_THAT(SuperHasPartition("", "boot"), IsOkAndValue(false));

  // Repack boot_a with a larger, non-4096-aligned payload (5000 bytes -> 8192
  // bytes aligned in super, with 3192 zero-padded bytes in the tail).
  std::string new_boot_payload(5000, 'K');
  std::string new_boot_path = absl::StrCat(temp_dir_, "/boot_repacked.img");
  ASSERT_THAT(WriteNewFile(new_boot_path, new_boot_payload), IsOk());

  ASSERT_THAT(RepackSuperWithPartition(*super_path, new_boot_path, "boot"),
              IsOk());
  EXPECT_THAT(IsSparseImage(*super_path), IsOkAndValue(false));

  std::unique_ptr<android::fs_mgr::LpMetadata> meta_slot0 =
      android::fs_mgr::ReadMetadata(*super_path, 0);
  std::unique_ptr<android::fs_mgr::LpMetadata> meta_slot1 =
      android::fs_mgr::ReadMetadata(*super_path, 1);
  ASSERT_NE(meta_slot0, nullptr);
  ASSERT_NE(meta_slot1, nullptr);

  const LpMetadataPartition* boot_part =
      android::fs_mgr::FindPartition(*meta_slot0, "boot_a");
  ASSERT_NE(boot_part, nullptr);
  EXPECT_EQ(boot_part->attributes, uint32_t{LP_PARTITION_ATTR_READONLY});
  ASSERT_LT(boot_part->group_index, meta_slot0->groups.size());
  EXPECT_EQ(android::fs_mgr::GetPartitionGroupName(
                meta_slot0->groups[boot_part->group_index]),
            "google_system_dynamic_partitions_a");
  EXPECT_NE(android::fs_mgr::FindPartition(*meta_slot1, "boot_a"), nullptr);

  // Extract boot and vendor via SuperImageAsBuild to verify exact contents and
  // zero-padded alignment tail.
  FakeAndroidBuild underlying;
  underlying.AddExtractedImage("super", *super_path);
  Result<std::unique_ptr<AndroidBuild>> super_build =
      SuperImageAsBuild(underlying);
  ASSERT_THAT(super_build, IsOk());
  ASSERT_THAT(
      (*super_build)->SetExtractDir(absl::StrCat(temp_dir_, "/extracted")),
      IsOk());

  Result<std::string> extracted_boot = (*super_build)->ImageFile("boot", true);
  ASSERT_THAT(extracted_boot, IsOk());
  std::string expected_padded_boot =
      absl::StrCat(new_boot_payload, std::string(8192 - 5000, '\0'));
  EXPECT_EQ(ReadFile(*extracted_boot), expected_padded_boot);

  Result<std::string> extracted_vendor =
      (*super_build)->ImageFile("vendor", true);
  ASSERT_THAT(extracted_vendor, IsOk());
  EXPECT_EQ(ReadFile(*extracted_vendor), orig_vendor);

  // Calling RepackSuperWithPartition again with identical input is idempotent
  // and does not touch inode or mtime.
  struct stat st_before{};
  ASSERT_EQ(stat(super_path->c_str(), &st_before), 0);
  ASSERT_THAT(RepackSuperWithPartition(*super_path, new_boot_path, "boot"),
              IsOk());
  struct stat st_after{};
  ASSERT_EQ(stat(super_path->c_str(), &st_after), 0);
  EXPECT_EQ(st_before.st_ino, st_after.st_ino);
  EXPECT_EQ(st_before.st_mtim.tv_sec, st_after.st_mtim.tv_sec);
  EXPECT_EQ(st_before.st_mtim.tv_nsec, st_after.st_mtim.tv_nsec);

  // Also verify adding a sparse partition image for a new vendor logical
  // partition (vbmeta_vendor_dlkm).
  std::string sparse_dlkm_data(4096, 'M');
  std::string sparse_dlkm_path =
      absl::StrCat(temp_dir_, "/vbmeta_vendor_dlkm_sparse.img");
  {
    std::unique_ptr<sparse_file, decltype(&sparse_file_destroy)> sf(
        sparse_file_new(4096, sparse_dlkm_data.size()), sparse_file_destroy);
    ASSERT_NE(sf, nullptr);
    ASSERT_EQ(sparse_file_add_data(sf.get(), sparse_dlkm_data.data(),
                                   sparse_dlkm_data.size(), 0),
              0);
    android::base::unique_fd out_fd(
        open(sparse_dlkm_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
             0644));
    ASSERT_TRUE(out_fd.ok());
    ASSERT_EQ(sparse_file_write(sf.get(), out_fd.get(), /* gz= */ false,
                                /* sparse= */ true, /* crc= */ false),
              0);
  }
  ASSERT_THAT(IsSparseImage(sparse_dlkm_path), IsOkAndValue(true));

  ASSERT_THAT(RepackSuperWithPartition(*super_path, sparse_dlkm_path,
                                       "vbmeta_vendor_dlkm"),
              IsOk());
  EXPECT_THAT(SuperHasPartition(*super_path, "vbmeta_vendor_dlkm"),
              IsOkAndValue(true));

  std::unique_ptr<android::fs_mgr::LpMetadata> meta_after =
      android::fs_mgr::ReadMetadata(*super_path, 0);
  ASSERT_NE(meta_after, nullptr);
  const LpMetadataPartition* dlkm_part =
      android::fs_mgr::FindPartition(*meta_after, "vbmeta_vendor_dlkm_a");
  ASSERT_NE(dlkm_part, nullptr);
  EXPECT_EQ(android::fs_mgr::GetPartitionGroupName(
                meta_after->groups[dlkm_part->group_index]),
            "google_vendor_dynamic_partitions_a");
}

}  // namespace
}  // namespace cuttlefish
