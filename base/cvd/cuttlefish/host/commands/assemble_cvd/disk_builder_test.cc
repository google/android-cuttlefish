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

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include "android-base/file.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/cf_endian.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/config/vmm_mode.h"
#include "cuttlefish/host/libs/image_aggregator/composite_disk.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"
#include "cuttlefish/host/libs/image_aggregator/qcow2.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

struct __attribute__((packed)) TestQcowHeader {
  Be32 magic;
  Be32 version;
  Be64 backing_file_offset;
  Be32 backing_file_size;
  Be32 cluster_bits;
  Be64 size;
  Be32 crypt_method;
  Be32 l1_size;
  Be64 l1_table_offset;
  Be64 refcount_table_offset;
  Be32 refcount_table_clusters;
  Be32 nb_snapshots;
  Be64 snapshots_offset;
};
static_assert(sizeof(TestQcowHeader) == 72);

class ScopedEnvVar {
 public:
  ScopedEnvVar(const char* name, const std::string& value) : name_(name) {
    const char* prev = getenv(name);
    if (prev != nullptr) {
      had_prev_ = true;
      prev_ = prev;
    }
    setenv(name, value.c_str(), 1);
  }
  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
  ~ScopedEnvVar() {
    if (had_prev_) {
      setenv(name_.c_str(), prev_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  bool had_prev_ = false;
  std::string prev_;
};

Result<void> WriteMinimalQcow2Image(const std::string& path,
                                    uint64_t virtual_size) {
  Fd fd =
      CF_EXPECT(Fd::Open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644));
  TestQcowHeader hdr{
      .magic = Be32(0x514649fbU),
      .version = Be32(3),
      .backing_file_offset = Be64(0),
      .backing_file_size = Be32(0),
      .cluster_bits = Be32(16),
      .size = Be64(virtual_size),
      .crypt_method = Be32(0),
      .l1_size = Be32(0),
      .l1_table_offset = Be64(0),
      .refcount_table_offset = Be64(0),
      .refcount_table_clusters = Be32(0),
      .nb_snapshots = Be32(0),
      .snapshots_offset = Be64(0),
  };
  CF_EXPECT(WriteExactBinary(fd, hdr));
  return {};
}

Result<void> WriteExecutableScript(const std::string& path,
                                   const std::string& body) {
  CF_EXPECT(android::base::WriteStringToFile(body, path));
  CF_EXPECT_EQ(chmod(path.c_str(), 0755), 0);
  return {};
}

DiskBuilder BaseQemuDiskBuilder(const std::string& dir,
                                const std::string& part_path) {
  return DiskBuilder()
      .Partitions(
          {ImagePartition{.label = "part_a", .image_file_path = part_path}})
      .VmManager(VmmMode::kQemu)
      .ConfigPath(dir + "/disk_config.txt")
      .HeaderPath(dir + "/gpt_header.img")
      .FooterPath(dir + "/gpt_footer.img")
      .CompositeDiskPath(dir + "/composite.img")
      .OverlayPath(dir + "/overlay.img")
      .ResumeIfPossible(true);
}

TEST(DiskBuilderTest, WillRebuildWhenVhostUserBlockOrReadOnlyToggles) {
  TemporaryDir tmp;
  const std::string dir = tmp.path;
  const std::string part = dir + "/part.img";
  ASSERT_TRUE(android::base::WriteStringToFile(std::string(4096, 'A'), part));

  DiskBuilder builder =
      BaseQemuDiskBuilder(dir, part).VhostUserBlock(true).ReadOnly(true);
  ASSERT_THAT(builder.BuildCompositeDiskIfNecessary(), IsOkAndValue(true));
  EXPECT_THAT(builder.WillRebuildCompositeDisk(), IsOkAndValue(false));
  EXPECT_THAT(CompositeDiskImage::OpenExisting(dir + "/composite.img"), IsOk());

  // Toggling ReadOnly (e.g., --use_overlay=false) must invalidate TextConfig.
  DiskBuilder rw_builder =
      BaseQemuDiskBuilder(dir, part).VhostUserBlock(true).ReadOnly(false);
  EXPECT_THAT(rw_builder.WillRebuildCompositeDisk(), IsOkAndValue(true));
  ASSERT_THAT(rw_builder.BuildCompositeDiskIfNecessary(), IsOkAndValue(true));
  EXPECT_THAT(CompositeDiskImage::OpenExisting(dir + "/composite.img"), IsOk());

  // Toggling VhostUserBlock to false must rebuild as a raw aggregated GPT disk.
  DiskBuilder raw_builder =
      BaseQemuDiskBuilder(dir, part).VhostUserBlock(false).ReadOnly(false);
  EXPECT_THAT(raw_builder.WillRebuildCompositeDisk(), IsOkAndValue(true));
  ASSERT_THAT(raw_builder.BuildCompositeDiskIfNecessary(), IsOkAndValue(true));
  EXPECT_THAT(CompositeDiskImage::OpenExisting(dir + "/composite.img"),
              IsError());
  EXPECT_GT(FileSize(dir + "/composite.img"), 4096U);
}

TEST(DiskBuilderTest, BuildOverlayUsesQemuImgWithoutCrosvmWhenVhostDisabled) {
  TemporaryDir tmp;
  const std::string dir = tmp.path;
  const std::string bin_dir = dir + "/bin";
  ASSERT_EQ(mkdir(bin_dir.c_str(), 0755), 0);

  const std::string qcow_template = dir + "/template.qcow2";
  ASSERT_THAT(WriteMinimalQcow2Image(qcow_template, /*virtual_size=*/65536ULL),
              IsOk());
  ASSERT_THAT(WriteExecutableScript(bin_dir + "/qemu-img",
                                    "#!/bin/sh\n"
                                    "for last; do :; done\n"
                                    "cp \"" +
                                        qcow_template + "\" \"$last\"\n"),
              IsOk());

  const char* orig_path = getenv("PATH");
  const std::string new_path =
      bin_dir + (orig_path != nullptr ? std::string(":") + orig_path : "");
  ScopedEnvVar path_env("PATH", new_path);

  const std::string part = dir + "/part.img";
  ASSERT_TRUE(android::base::WriteStringToFile(std::string(4096, 'B'), part));

  DiskBuilder raw_qemu_builder = BaseQemuDiskBuilder(dir, part)
                                     .CrosvmPath("")
                                     .VhostUserBlock(false)
                                     .ReadOnly(true);
  ASSERT_THAT(raw_qemu_builder.BuildCompositeDiskIfNecessary(),
              IsOkAndValue(true));
  ASSERT_THAT(raw_qemu_builder.BuildOverlayIfNecessary(), IsOkAndValue(true));
  EXPECT_THAT(Qcow2Image::OpenExisting(dir + "/overlay.img"), IsOk());

  // With VhostUserBlock(true), an empty CrosvmPath must fail instead of
  // silently invoking qemu-img on a composite_disk descriptor.
  DiskBuilder vhost_builder = BaseQemuDiskBuilder(dir, part)
                                  .CrosvmPath("")
                                  .VhostUserBlock(true)
                                  .ReadOnly(true)
                                  .OverlayPath(dir + "/vhost_overlay.img");
  ASSERT_THAT(vhost_builder.BuildCompositeDiskIfNecessary(),
              IsOkAndValue(true));
  EXPECT_THAT(vhost_builder.BuildOverlayIfNecessary(), IsError());
}

}  // namespace
}  // namespace cuttlefish
