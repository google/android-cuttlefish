/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/libs/image_aggregator/qcow2.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/cf_endian.h"
#include "cuttlefish/io/read_exact.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/managed_stdio.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

namespace {

struct __attribute__((packed)) QcowHeader {
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

static_assert(sizeof(QcowHeader) == 72);

Result<std::string> DetectQemuBackingFormat(const std::string& backing_file) {
  Fd fd = CF_EXPECT(Fd::Open(backing_file, O_CLOEXEC | O_RDONLY));
  const off_t size = fd.LSeek(0, SEEK_END);
  CF_EXPECTF(size >= 0, "Failed to seek backing file '{}'", backing_file);
  char magic[15] = {};
  const size_t to_read =
      std::min<size_t>(sizeof(magic), static_cast<size_t>(size));
  if (to_read > 0) {
    CF_EXPECT(PReadExact(fd, magic, to_read, 0));
  }
  const std::string_view magic_sv(magic, to_read);
  constexpr std::string_view kCompositeMagic = "composite_disk\x1d";
  CF_EXPECT(!magic_sv.starts_with(kCompositeMagic),
            "qemu-img cannot use a crosvm composite_disk backing file");
  if (magic_sv.starts_with(Qcow2Image::MagicString())) {
    return "qcow2";
  }
  return "raw";
}

Command QemuImgCommand(std::string_view backing_format,
                       std::string_view backing_file,
                       std::string_view output_overlay_path) {
  return Command("qemu-img")
      .AddParameter("create")
      .AddParameter("-q")
      .AddParameter("-f")
      .AddParameter("qcow2")
      .AddParameter("-F")
      .AddParameter(backing_format)
      .AddParameter("-b")
      .AddParameter(backing_file)
      .AddParameter(output_overlay_path);
}

Command CrosvmCreateQcow2Command(const std::string& crosvm_path,
                                 std::string_view backing_file,
                                 std::string_view output_overlay_path) {
  return Command(crosvm_path)
      .AddParameter("create_qcow2")
      .AddParameter("--backing-file")
      .AddParameter(backing_file)
      .AddParameter(output_overlay_path);
}

}  // namespace

struct Qcow2Image::Impl {
  QcowHeader header_;
};

Result<Qcow2Image> Qcow2Image::CreateByCrosvm(const std::string& crosvm_path,
                                              const std::string& backing_file,
                                              std::string output_overlay_path) {
  CF_EXPECT(!crosvm_path.empty(), "crosvm binary missing");
  Command create_cmd =
      CrosvmCreateQcow2Command(crosvm_path, backing_file, output_overlay_path);
  CF_EXPECT(RunAndCaptureStdout(std::move(create_cmd)));
  return CF_EXPECT(OpenExisting(std::move(output_overlay_path)));
}

Result<Qcow2Image> Qcow2Image::CreateByQemuImg(
    const std::string& backing_file, std::string output_overlay_path) {
  const std::string backing_format =
      CF_EXPECT(DetectQemuBackingFormat(backing_file));
  Command create_cmd =
      QemuImgCommand(backing_format, backing_file, output_overlay_path);
  CF_EXPECT(RunAndCaptureStdout(std::move(create_cmd)));
  return CF_EXPECT(OpenExisting(std::move(output_overlay_path)));
}

Result<Qcow2Image> Qcow2Image::OpenExisting(std::string path) {
  Fd fd = CF_EXPECT(Fd::Open(path, O_CLOEXEC, O_RDONLY));

  std::unique_ptr<Impl> impl(CF_EXPECT(new Impl()));

  impl->header_ = CF_EXPECT(ReadExactBinary<QcowHeader>(fd));

  std::string magic(reinterpret_cast<char*>(&impl->header_.magic),
                    MagicString().size());
  CF_EXPECT_EQ(magic, MagicString());

  return Qcow2Image(std::move(impl));
}

std::string Qcow2Image::MagicString() { return "QFI\xfb"; }

Qcow2Image::Qcow2Image(Qcow2Image&& other) { impl_ = std::move(other.impl_); }
Qcow2Image::~Qcow2Image() = default;
Qcow2Image& Qcow2Image::operator=(Qcow2Image&& other) {
  impl_ = std::move(other.impl_);
  return *this;
}

Result<uint64_t> Qcow2Image::VirtualSizeBytes() const {
  CF_EXPECT(impl_.get());

  return impl_->header_.size.as_uint64_t();
}

Qcow2Image::Qcow2Image(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

}  // namespace cuttlefish
