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
#include "cuttlefish/host/libs/zip/lazily_loaded_file.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>  // NOLINT(misc-include-cleaner): SEEK_SET

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <android-base/logging.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/disjoint_range_set.h"
#include "cuttlefish/host/libs/zip/serialize_disjoint_range_set.h"

namespace cuttlefish {

LazilyLoadedFileReadCallback::~LazilyLoadedFileReadCallback() = default;

struct LazilyLoadedFile::Impl {
  std::string MetadataFile() const;
  Result<void> ReadMetadata();
  Result<void> WriteMetadata();

  Result<size_t> Read(char*, size_t);

  std::string filename_;
  SharedFD contents_file_;
  std::unique_ptr<LazilyLoadedFileReadCallback> callback_;
  DisjointRangeSet already_downloaded_;
  size_t seek_pos_;
};

Result<LazilyLoadedFile> LazilyLoadedFile::Create(
    std::string filename,
    std::unique_ptr<LazilyLoadedFileReadCallback> callback) {
  std::unique_ptr<Impl> impl = std::make_unique<Impl>();
  CF_EXPECT(impl.get());

  impl->contents_file_ = SharedFD::Open(filename, O_CREAT | O_RDWR, 0644);
  impl->filename_ = std::move(filename);
  impl->callback_ = std::move(callback);
  impl->seek_pos_ = 0;

  CF_EXPECT(impl->ReadMetadata());

  return LazilyLoadedFile(std::move(impl));
}

LazilyLoadedFile::LazilyLoadedFile(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

LazilyLoadedFile::LazilyLoadedFile(LazilyLoadedFile&& other) {
  std::swap(impl_, other.impl_);
}

LazilyLoadedFile::~LazilyLoadedFile() {
  if (!impl_) {
    return;
  }
  Result<void> res = impl_->WriteMetadata();
  if (!res.ok()) {
    LOG(WARNING) << "fragment update failure: " << res.error().FormatForEnv();
  }
}

LazilyLoadedFile& LazilyLoadedFile::operator=(LazilyLoadedFile&& other) {
  impl_.reset();
  std::swap(impl_, other.impl_);
  return *this;
}

Result<size_t> LazilyLoadedFile::Read(char* data, size_t size) {
  CF_EXPECT(impl_.get());
  return CF_EXPECT(impl_->Read(data, size));
}

Result<void> LazilyLoadedFile::Seek(size_t location) {
  CF_EXPECT(impl_.get());
  impl_->seek_pos_ = location;
  return {};
}

std::string LazilyLoadedFile::Impl::MetadataFile() const {
  return filename_ + ".frag_data";
}

Result<void> LazilyLoadedFile::Impl::ReadMetadata() {
  SharedFD metadata_fd = SharedFD::Open(MetadataFile(), O_CREAT | O_RDWR, 0644);
  CF_EXPECTF(metadata_fd->IsOpen(), "Failed to open {}: {}", MetadataFile(),
             metadata_fd->StrError());

  std::string data;
  CF_EXPECT_GE(ReadAll(metadata_fd, &data), 0, metadata_fd->StrError());

  Result<DisjointRangeSet> parsed_res = DeserializeDisjointRangeSet(data);
  if (parsed_res.ok()) {
    already_downloaded_ = std::move(*parsed_res);
  } else {
    LOG(WARNING) << "Invalid fragments: " << parsed_res.error().FormatForEnv();
  }

  return {};
}

Result<void> LazilyLoadedFile::Impl::WriteMetadata() {
  std::string new_metadata_name = MetadataFile() + ".XXXXXX";
  SharedFD new_metadata = SharedFD::Mkstemp(&new_metadata_name);
  CF_EXPECT(new_metadata->IsOpen(), new_metadata->StrError());
  CF_EXPECT(new_metadata->Chmod(0644));

  std::string data = Serialize(already_downloaded_);
  CF_EXPECT_EQ(WriteAll(new_metadata, data), data.size(),
               new_metadata->StrError());

  CF_EXPECT(RenameFile(new_metadata_name, MetadataFile()));

  return {};
}

Result<size_t> LazilyLoadedFile::Impl::Read(char* data, size_t size) {
  // NOLINTNEXTLINE(misc-include-cleaner): SEEK_SET
  CF_EXPECT_EQ(contents_file_->LSeek(seek_pos_, SEEK_SET), seek_pos_,
               contents_file_->StrError());
  std::optional<uint64_t> end_of_present_data =
      already_downloaded_.EndOfContainingRange(seek_pos_);
  // In terms of IO performance, this aims to minimize round trips over
  // minimizing bandwidth usage.
  if (end_of_present_data.has_value()) {
    size_t read_request = std::min(*end_of_present_data - seek_pos_, size);
    ssize_t data_read = contents_file_->Read(data, read_request);
    CF_EXPECT_GE(data_read, 0, contents_file_->StrError());
    seek_pos_ += data_read;
    return data_read;
  }
  CF_EXPECT(callback_->Seek(seek_pos_));
  size_t data_read = CF_EXPECT(callback_->Read(data, size));
  CF_EXPECT_EQ(WriteAll(contents_file_, data, data_read), data_read,
               contents_file_->StrError());
  already_downloaded_.InsertRange(seek_pos_, seek_pos_ + data_read);
  seek_pos_ += data_read;
  return data_read;
}

}  // namespace cuttlefish
