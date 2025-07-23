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

#include <string>
#include <utility>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/cf_endian.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"

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

}  // namespace

struct Qcow2Image::Impl {
  QcowHeader header_;
};

Result<Qcow2Image> Qcow2Image::Create(const std::string& crosvm_path,
                                      const std::string& backing_file,
                                      std::string output_overlay_path) {
  Command create_cmd = Command(crosvm_path)
                           .AddParameter("create_qcow2")
                           .AddParameter("--backing-file")
                           .AddParameter(backing_file)
                           .AddParameter(output_overlay_path);

  std::string stdout_str;
  std::string stderr_str;
  int return_code = RunWithManagedStdio(std::move(create_cmd), nullptr,
                                        &stdout_str, &stderr_str);
  CF_EXPECT_EQ(return_code, 0,
               "Failed to run `"
                   << crosvm_path << " create_qcow2 --backing-file "
                   << backing_file << " " << output_overlay_path << "`"
                   << "stdout:\n###\n"
                   << stdout_str << "\n###"
                   << "stderr:\n###\n"
                   << stderr_str << "\n###");

  return CF_EXPECT(OpenExisting(std::move(output_overlay_path)));
}

Result<Qcow2Image> Qcow2Image::OpenExisting(std::string path) {
  SharedFD fd = SharedFD::Open(path, O_CLOEXEC, O_RDONLY);
  CF_EXPECT(fd->IsOpen(), fd->StrError());

  std::unique_ptr<Impl> impl(new Impl());
  CF_EXPECT(impl.get());

  CF_EXPECT_EQ(ReadExactBinary(fd, &impl->header_), sizeof(QcowHeader));

  std::string magic(reinterpret_cast<char*>(&impl->header_),
                    MagicString().size());
  CF_EXPECT_EQ(magic, MagicString());

  return Qcow2Image(std::move(impl));
}

std::string Qcow2Image::MagicString() { return "QFI\xfb"; }

Qcow2Image::Qcow2Image(Qcow2Image&& other) {
  impl_ = std::move(other.impl_);
}
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
