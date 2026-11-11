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

#include "cuttlefish/host/commands/assemble_cvd/disk_builder.h"

#include <memory>
#include <string>
#include <vector>

#include "android-base/file.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/image_aggregator/composite_disk.h"
#include "cuttlefish/host/libs/image_aggregator/disk_image.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"
#include "cuttlefish/host/libs/image_aggregator/image_from_file.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

DiskBuilder BaseDiskBuilder(const std::string& dir,
                            const std::string& part_path) {
  return DiskBuilder()
      .Partitions(
          {ImagePartition{.label = "part_a", .image_file_path = part_path}})
      .ConfigPath(dir + "/disk_config.txt")
      .HeaderPath(dir + "/gpt_header.img")
      .FooterPath(dir + "/gpt_footer.img")
      .CompositeDiskPath(dir + "/composite.img")
      .OverlayPath(dir + "/overlay.img")
      .ResumeIfPossible(true);
}

TEST(DiskBuilderTest, WillRebuildWhenCompositeDiskSupportOrReadOnlyToggles) {
  TemporaryDir tmp;
  const std::string dir = tmp.path;
  const std::string part = dir + "/part.img";
  ASSERT_TRUE(android::base::WriteStringToFile(std::string(4096, 'A'), part));

  EXPECT_THAT(BaseDiskBuilder(dir, part).BuildCompositeDiskIfNecessary(),
              IsError());

  DiskBuilder builder =
      BaseDiskBuilder(dir, part).VmmSupportsCompositeDisks(true).ReadOnly(true);
  ASSERT_THAT(builder.BuildCompositeDiskIfNecessary(), IsOkAndValue(true));
  EXPECT_THAT(builder.WillRebuildCompositeDisk(), IsOkAndValue(false));
  EXPECT_THAT(CompositeDiskImage::OpenExisting(dir + "/composite.img"), IsOk());

  // Toggling ReadOnly (e.g., --use_overlay=false) must invalidate TextConfig.
  DiskBuilder rw_builder =
      BaseDiskBuilder(dir, part).VmmSupportsCompositeDisks(true).ReadOnly(
          false);
  EXPECT_THAT(rw_builder.WillRebuildCompositeDisk(), IsOkAndValue(true));
  ASSERT_THAT(rw_builder.BuildCompositeDiskIfNecessary(), IsOkAndValue(true));
  EXPECT_THAT(CompositeDiskImage::OpenExisting(dir + "/composite.img"), IsOk());

  // Toggling VmmSupportsCompositeDisks to false must rebuild as a raw
  // aggregated GPT disk.
  DiskBuilder raw_builder =
      BaseDiskBuilder(dir, part).VmmSupportsCompositeDisks(false).ReadOnly(
          false);
  EXPECT_THAT(raw_builder.WillRebuildCompositeDisk(), IsOkAndValue(true));
  ASSERT_THAT(raw_builder.BuildCompositeDiskIfNecessary(), IsOkAndValue(true));
  EXPECT_THAT(CompositeDiskImage::OpenExisting(dir + "/composite.img"),
              IsError());
  EXPECT_GT(FileSize(dir + "/composite.img"), 4096U);
  std::unique_ptr<DiskImage> raw_image =
      CF_ASSERT(ImageFromFile(dir + "/composite.img"));
  EXPECT_THAT(raw_image->QemuImgName(), IsOkAndValue("raw"));
}

TEST(DiskBuilderTest, BuildOverlayRequiresCrosvmWhenCompositeDisksSupported) {
  TemporaryDir tmp;
  const std::string dir = tmp.path;
  const std::string part = dir + "/part.img";
  ASSERT_TRUE(android::base::WriteStringToFile(std::string(4096, 'B'), part));

  DiskBuilder vhost_builder = BaseDiskBuilder(dir, part)
                                  .CrosvmPath("")
                                  .VmmSupportsCompositeDisks(true)
                                  .ReadOnly(true)
                                  .OverlayPath(dir + "/vhost_overlay.img");
  ASSERT_THAT(vhost_builder.BuildCompositeDiskIfNecessary(),
              IsOkAndValue(true));
  EXPECT_THAT(vhost_builder.BuildOverlayIfNecessary(), IsError());
}

}  // namespace
}  // namespace cuttlefish
