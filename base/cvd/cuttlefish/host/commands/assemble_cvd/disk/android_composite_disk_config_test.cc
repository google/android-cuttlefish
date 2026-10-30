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

#include "cuttlefish/host/commands/assemble_cvd/disk/android_composite_disk_config.h"

#include <stdlib.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gflags/gflags.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/recursively_remove_directory.h"
#include "cuttlefish/host/commands/assemble_cvd/android_build/fake_android_build.h"
#include "cuttlefish/host/commands/assemble_cvd/disk/image_file.h"
#include "cuttlefish/host/commands/assemble_cvd/flags/system_image_dir.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/host/libs/image_aggregator/gpt_type_guid.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

class FixedImageFile : public ImageFile {
 public:
  FixedImageFile(std::string name, std::string path)
      : name_(std::move(name)), path_(std::move(path)) {}

  std::string Name() const override { return name_; }
  Result<std::string> Generate() override { return path_; }
  Result<std::string> Path() const override { return path_; }

 private:
  std::string name_;
  std::string path_;
};

class AndroidCompositeDiskConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string tmpl = "/tmp/cf_composite_disk_test_XXXXXX";
    char* created = mkdtemp(tmpl.data());
    ASSERT_NE(created, nullptr);
    temp_dir_ = created;

    gflags::SetCommandLineOption("system_image_dir", temp_dir_.c_str());
    gflags::SetCommandLineOption("experimental_build_super_image", "false");
  }

  void TearDown() override {
    if (!temp_dir_.empty()) {
      (void)RecursivelyRemoveDirectory(temp_dir_);
    }
  }

  Result<std::string> TouchImage(const std::string& filename) {
    std::string path = absl::StrCat(temp_dir_, "/", filename);
    CF_EXPECT(WriteNewFile(path, "stub"));
    return path;
  }

  std::string temp_dir_;
};

TEST_F(AndroidCompositeDiskConfigTest,
       OmitsAllLogicalPartitionsFromPhysicalGpt) {
  Result<std::string> super_path = TouchImage("super.img");
  Result<std::string> userdata_path = TouchImage("userdata.img");
  Result<std::string> metadata_path = TouchImage("metadata.img");
  Result<std::string> misc_path = TouchImage("misc.img");
  Result<std::string> esp_path = TouchImage("android_esp.img");
  Result<std::string> extracted_boot = TouchImage("extracted_boot.img");
  Result<std::string> extracted_init_boot =
      TouchImage("extracted_init_boot.img");
  Result<std::string> extracted_vendor_boot =
      TouchImage("extracted_vendor_boot.img");
  Result<std::string> extracted_vbmeta = TouchImage("extracted_vbmeta.img");
  Result<std::string> extracted_vbmeta_system =
      TouchImage("extracted_vbmeta_system.img");
  ASSERT_THAT(super_path, IsOk());
  ASSERT_THAT(userdata_path, IsOk());
  ASSERT_THAT(metadata_path, IsOk());
  ASSERT_THAT(misc_path, IsOk());
  ASSERT_THAT(esp_path, IsOk());
  ASSERT_THAT(extracted_boot, IsOk());
  ASSERT_THAT(extracted_init_boot, IsOk());
  ASSERT_THAT(extracted_vendor_boot, IsOk());
  ASSERT_THAT(extracted_vbmeta, IsOk());
  ASSERT_THAT(extracted_vbmeta_system, IsOk());

  CuttlefishConfig config;
  config.set_root_dir(temp_dir_);
  auto instance = config.ForInstance(1);
  instance.set_super_image(*super_path);
  instance.set_data_image(*userdata_path);
  // Even if instance config fields point to extracted logical images for host
  // consumers, logical partitions must not be emitted into the physical GPT.
  instance.set_init_boot_image(*extracted_init_boot);
  instance.set_new_vendor_boot_image(*extracted_vendor_boot);
  instance.set_new_vbmeta_image(*extracted_vbmeta);
  instance.set_vbmeta_system_image(*extracted_vbmeta_system);

  std::vector<std::unique_ptr<ImageFile>> image_files;
  image_files.emplace_back(
      std::make_unique<FixedImageFile>("android_esp", *esp_path));
  image_files.emplace_back(
      std::make_unique<FixedImageFile>("metadata", *metadata_path));
  image_files.emplace_back(
      std::make_unique<FixedImageFile>("misc", *misc_path));

  FakeAndroidBuild build;
  build.AddExtractedImage("super", *super_path);
  build.AddExtractedImage("userdata", *userdata_path);
  build.SetPhysicalPartitions({"super", "userdata"});
  build.SetLogicalPartitions({
      "boot",
      "init_boot",
      "odm",
      "odm_dlkm",
      "product",
      "system",
      "system_dlkm",
      "system_ext",
      "vbmeta",
      "vbmeta_system",
      "vbmeta_system_dlkm",
      "vbmeta_vendor_dlkm",
      "vendor",
      "vendor_dlkm",
      "vendor_boot",
  });

  Result<SystemImageDirFlag> system_image_dir =
      SystemImageDirFlag::FromGlobalGflags();
  ASSERT_THAT(system_image_dir, IsOk());

  const CuttlefishConfig& const_config = config;
  Result<std::vector<ImagePartition>> partitions = AndroidCompositeDiskConfig(
      const_config.ForInstance(1), image_files, build, *system_image_dir);
  ASSERT_THAT(partitions, IsOk());

  std::set<std::string> labels;
  for (const ImagePartition& part : *partitions) {
    labels.insert(part.label);
    if (part.label == "android_esp") {
      EXPECT_EQ(part.type, GptPartitionType::kEfiSystemPartition);
    }
  }

  std::set<std::string> expected_labels = {
      "android_esp", "metadata", "misc", "super", "userdata",
  };
  EXPECT_EQ(labels, expected_labels);
}

TEST_F(AndroidCompositeDiskConfigTest,
       IncludesSlottedBootAndVbmetaWhenPhysical) {
  Result<std::string> super_path = TouchImage("super.img");
  Result<std::string> userdata_path = TouchImage("userdata.img");
  Result<std::string> metadata_path = TouchImage("metadata.img");
  Result<std::string> misc_path = TouchImage("misc.img");
  Result<std::string> boot_path = TouchImage("boot.img");
  Result<std::string> init_boot_path = TouchImage("init_boot.img");
  Result<std::string> vendor_boot_path = TouchImage("vendor_boot.img");
  Result<std::string> vbmeta_path = TouchImage("vbmeta.img");
  Result<std::string> vbmeta_system_path = TouchImage("vbmeta_system.img");
  ASSERT_THAT(super_path, IsOk());
  ASSERT_THAT(userdata_path, IsOk());
  ASSERT_THAT(metadata_path, IsOk());
  ASSERT_THAT(misc_path, IsOk());
  ASSERT_THAT(boot_path, IsOk());
  ASSERT_THAT(init_boot_path, IsOk());
  ASSERT_THAT(vendor_boot_path, IsOk());
  ASSERT_THAT(vbmeta_path, IsOk());
  ASSERT_THAT(vbmeta_system_path, IsOk());

  CuttlefishConfig config;
  config.set_root_dir(temp_dir_);
  auto instance = config.ForInstance(1);
  instance.set_super_image(*super_path);
  instance.set_data_image(*userdata_path);
  instance.set_init_boot_image(*init_boot_path);
  instance.set_new_vendor_boot_image(*vendor_boot_path);
  instance.set_new_vbmeta_image(*vbmeta_path);
  instance.set_vbmeta_system_image(*vbmeta_system_path);

  std::vector<std::unique_ptr<ImageFile>> image_files;
  image_files.emplace_back(
      std::make_unique<FixedImageFile>("metadata", *metadata_path));
  image_files.emplace_back(
      std::make_unique<FixedImageFile>("misc", *misc_path));

  FakeAndroidBuild build;
  build.AddExtractedImage("boot", *boot_path);
  build.AddExtractedImage("init_boot", *init_boot_path);
  build.AddExtractedImage("vendor_boot", *vendor_boot_path);
  build.AddExtractedImage("vbmeta", *vbmeta_path);
  build.AddExtractedImage("vbmeta_system", *vbmeta_system_path);
  build.AddExtractedImage("super", *super_path);
  build.AddExtractedImage("userdata", *userdata_path);
  build.SetPhysicalPartitions({
      "boot",
      "init_boot",
      "super",
      "userdata",
      "vbmeta",
      "vbmeta_system",
      "vendor_boot",
  });
  build.SetLogicalPartitions({"product", "system", "vendor"});

  Result<SystemImageDirFlag> system_image_dir =
      SystemImageDirFlag::FromGlobalGflags();
  ASSERT_THAT(system_image_dir, IsOk());

  const CuttlefishConfig& const_config = config;
  Result<std::vector<ImagePartition>> partitions = AndroidCompositeDiskConfig(
      const_config.ForInstance(1), image_files, build, *system_image_dir);
  ASSERT_THAT(partitions, IsOk());

  std::set<std::string> labels;
  for (const ImagePartition& part : *partitions) {
    labels.insert(part.label);
  }

  std::set<std::string> expected_labels = {
      "boot_a",        "boot_b",        "init_boot_a",     "init_boot_b",
      "metadata",      "misc",          "super",           "userdata",
      "vbmeta_a",      "vbmeta_b",      "vbmeta_system_a", "vbmeta_system_b",
      "vendor_boot_a", "vendor_boot_b",
  };
  EXPECT_EQ(labels, expected_labels);
}

}  // namespace
}  // namespace cuttlefish
