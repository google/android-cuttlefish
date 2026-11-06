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
#include <stdlib.h>
#include <sys/stat.h>

#include <string>

#include "android-base/file.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/cf_endian.h"
#include "cuttlefish/io/write_exact.h"
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

TEST(Qcow2ImageTest, OpenExistingAndRejectInvalidHeaders) {
  TemporaryDir tmp;
  const std::string dir = tmp.path;

  const std::string valid_qcow2 = dir + "/valid.qcow2";
  ASSERT_THAT(WriteMinimalQcow2Image(valid_qcow2, /*virtual_size=*/1048576ULL),
              IsOk());
  auto opened = Qcow2Image::OpenExisting(valid_qcow2);
  ASSERT_THAT(opened, IsOk());
  EXPECT_THAT(opened->VirtualSizeBytes(), IsOkAndValue(1048576ULL));

  const std::string truncated = dir + "/truncated.qcow2";
  ASSERT_TRUE(android::base::WriteStringToFile("QFI\xfb", truncated));
  EXPECT_THAT(Qcow2Image::OpenExisting(truncated), IsError());

  const std::string bad_magic = dir + "/bad_magic.qcow2";
  ASSERT_TRUE(
      android::base::WriteStringToFile(std::string(72, 'X'), bad_magic));
  EXPECT_THAT(Qcow2Image::OpenExisting(bad_magic), IsError());
}

TEST(Qcow2ImageTest, CreateByCrosvmAndQemuImg) {
  TemporaryDir tmp;
  const std::string dir = tmp.path;
  const std::string bin_dir = dir + "/bin";
  ASSERT_EQ(mkdir(bin_dir.c_str(), 0755), 0);

  const std::string qcow_template = dir + "/template.qcow2";
  ASSERT_THAT(WriteMinimalQcow2Image(qcow_template, /*virtual_size=*/65536ULL),
              IsOk());

  const std::string args_log = dir + "/args.txt";
  const std::string crosvm_stub = bin_dir + "/crosvm";
  ASSERT_THAT(WriteExecutableScript(crosvm_stub,
                                    "#!/bin/sh\n"
                                    "printf '%s ' \"$@\" > \"" +
                                        args_log +
                                        "\"\n"
                                        "for last; do :; done\n"
                                        "cp \"" +
                                        qcow_template + "\" \"$last\"\n"),
              IsOk());
  ASSERT_THAT(WriteExecutableScript(bin_dir + "/qemu-img",
                                    "#!/bin/sh\n"
                                    "printf '%s ' \"$@\" > \"" +
                                        args_log +
                                        "\"\n"
                                        "for last; do :; done\n"
                                        "cp \"" +
                                        qcow_template + "\" \"$last\"\n"),
              IsOk());

  const char* orig_path = getenv("PATH");
  const std::string new_path =
      bin_dir + (orig_path != nullptr ? std::string(":") + orig_path : "");
  ScopedEnvVar path_env("PATH", new_path);

  const std::string raw_backing = dir + "/raw.img";
  ASSERT_TRUE(
      android::base::WriteStringToFile(std::string(4096, '\0'), raw_backing));

  EXPECT_THAT(Qcow2Image::CreateByCrosvm("", raw_backing, dir + "/empty.qcow2"),
              IsError());

  const std::string crosvm_overlay = dir + "/crosvm_overlay.qcow2";
  ASSERT_THAT(
      Qcow2Image::CreateByCrosvm(crosvm_stub, raw_backing, crosvm_overlay),
      IsOk());
  std::string recorded;
  ASSERT_TRUE(android::base::ReadFileToString(args_log, &recorded));
  EXPECT_THAT(recorded, HasSubstr("create_qcow2 --backing-file"));

  const std::string qemu_raw_overlay = dir + "/qemu_raw.qcow2";
  ASSERT_THAT(Qcow2Image::CreateByQemuImg(raw_backing, qemu_raw_overlay),
              IsOk());
  ASSERT_TRUE(android::base::ReadFileToString(args_log, &recorded));
  EXPECT_THAT(recorded, HasSubstr("-F raw"));

  const std::string qcow_backing = dir + "/backing.qcow2";
  ASSERT_THAT(WriteMinimalQcow2Image(qcow_backing, /*virtual_size=*/65536ULL),
              IsOk());
  const std::string qemu_qcow_overlay = dir + "/qemu_qcow.qcow2";
  ASSERT_THAT(Qcow2Image::CreateByQemuImg(qcow_backing, qemu_qcow_overlay),
              IsOk());
  ASSERT_TRUE(android::base::ReadFileToString(args_log, &recorded));
  EXPECT_THAT(recorded, HasSubstr("-F qcow2"));

  const std::string cdisk_backing = dir + "/composite.img";
  ASSERT_TRUE(android::base::WriteStringToFile("composite_disk\x1dproto",
                                               cdisk_backing));
  EXPECT_THAT(
      Qcow2Image::CreateByQemuImg(cdisk_backing, dir + "/bad_cdisk.qcow2"),
      IsError());
}

}  // namespace
}  // namespace cuttlefish
