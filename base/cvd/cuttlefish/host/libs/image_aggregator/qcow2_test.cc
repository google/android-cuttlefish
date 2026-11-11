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

#include "cuttlefish/host/libs/image_aggregator/qcow2.h"

#include <fcntl.h>
#include <stdint.h>

#include <memory>
#include <string>

#include "android-base/file.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/cf_endian.h"
#include "cuttlefish/host/libs/image_aggregator/disk_image.h"
#include "cuttlefish/host/libs/image_aggregator/image_aggregator.h"
#include "cuttlefish/host/libs/image_aggregator/image_from_file.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/result/assert.h"
#include "cuttlefish/result/result.h"
#include "cuttlefish/result/result_matchers.h"

namespace cuttlefish {
namespace {

using ::testing::HasSubstr;

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

TEST(Qcow2ImageTest, OpenExistingAndRejectInvalidHeaders) {
  TemporaryDir tmp;
  const std::string dir = tmp.path;

  const std::string valid_qcow2 = dir + "/valid.qcow2";
  ASSERT_THAT(WriteMinimalQcow2Image(valid_qcow2, /*virtual_size=*/1048576ULL),
              IsOk());
  Qcow2Image opened = CF_ASSERT(Qcow2Image::OpenExisting(valid_qcow2));
  EXPECT_THAT(opened.VirtualSizeBytes(), IsOkAndValue(1048576ULL));
  EXPECT_THAT(opened.QemuImgName(), IsOkAndValue("qcow2"));

  const std::string truncated = dir + "/truncated.qcow2";
  ASSERT_TRUE(android::base::WriteStringToFile("QFI\xfb", truncated));
  EXPECT_THAT(Qcow2Image::OpenExisting(truncated), IsError());

  const std::string bad_magic = dir + "/bad_magic.qcow2";
  ASSERT_TRUE(
      android::base::WriteStringToFile(std::string(72, 'X'), bad_magic));
  EXPECT_THAT(Qcow2Image::OpenExisting(bad_magic), IsError());
}

TEST(Qcow2ImageTest, CreateByCrosvmAndQemuImgCommands) {
  TemporaryDir tmp;
  const std::string dir = tmp.path;

  const std::string raw_backing = dir + "/raw.img";
  ASSERT_TRUE(
      android::base::WriteStringToFile(std::string(4096, '\0'), raw_backing));
  std::unique_ptr<DiskImage> raw_image = CF_ASSERT(ImageFromFile(raw_backing));

  EXPECT_THAT(
      Qcow2Image::CreateByCrosvmCommand("", raw_backing, dir + "/empty.qcow2"),
      IsError());

  const std::string crosvm_overlay = dir + "/crosvm_overlay.qcow2";
  Command crosvm_cmd = CF_ASSERT(Qcow2Image::CreateByCrosvmCommand(
      "/path/to/crosvm", raw_backing, crosvm_overlay));
  EXPECT_EQ(crosvm_cmd.Executable(), "/path/to/crosvm");
  EXPECT_THAT(crosvm_cmd.AsBashScript(),
              HasSubstr("/path/to/crosvm \\\ncreate_qcow2 \\\n--backing-file "
                        "\\\n" +
                        raw_backing + " \\\n" + crosvm_overlay));

  const std::string qemu_raw_overlay = dir + "/qemu_raw.qcow2";
  Command qemu_raw_cmd = CF_ASSERT(Qcow2Image::CreateByQemuImgCommand(
      *raw_image, raw_backing, qemu_raw_overlay));
  EXPECT_THAT(qemu_raw_cmd.AsBashScript(),
              HasSubstr("qemu-img \\\ncreate \\\n-q \\\n-f \\\nqcow2 \\\n-F "
                        "\\\nraw \\\n-b \\\n" +
                        raw_backing + " \\\n" + qemu_raw_overlay));

  const std::string qcow_backing = dir + "/backing.qcow2";
  ASSERT_THAT(WriteMinimalQcow2Image(qcow_backing, /*virtual_size=*/65536ULL),
              IsOk());
  std::unique_ptr<DiskImage> qcow_image =
      CF_ASSERT(ImageFromFile(qcow_backing));
  const std::string qemu_qcow_overlay = dir + "/qemu_qcow.qcow2";
  Command qemu_qcow_cmd = CF_ASSERT(Qcow2Image::CreateByQemuImgCommand(
      *qcow_image, qcow_backing, qemu_qcow_overlay));
  EXPECT_THAT(qemu_qcow_cmd.AsBashScript(),
              HasSubstr("qemu-img \\\ncreate \\\n-q \\\n-f \\\nqcow2 \\\n-F "
                        "\\\nqcow2 \\\n-b \\\n" +
                        qcow_backing + " \\\n" + qemu_qcow_overlay));

  const std::string cdisk_backing = dir + "/composite.img";
  ASSERT_THAT(
      CreateOrUpdateCompositeDisk(
          {ImagePartition{.label = "part_a", .image_file_path = raw_backing}},
          dir + "/gpt_header.img", dir + "/gpt_footer.img", cdisk_backing,
          /*read_only=*/true),
      IsOk());
  std::unique_ptr<DiskImage> cdisk_image =
      CF_ASSERT(ImageFromFile(cdisk_backing));
  EXPECT_THAT(Qcow2Image::CreateByQemuImgCommand(*cdisk_image, cdisk_backing,
                                                 dir + "/bad_cdisk.qcow2"),
              IsError());
}

}  // namespace
}  // namespace cuttlefish
