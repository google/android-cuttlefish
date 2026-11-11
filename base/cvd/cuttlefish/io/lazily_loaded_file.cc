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

#include "cuttlefish/io/lazily_loaded_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/io/disjoint_range_set.h"
#include "cuttlefish/io/fake_seek.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/pwrite_exact.h"
#include "cuttlefish/io/reader_seeker.h"
#include "cuttlefish/io/serialize_disjoint_range_set.h"
#include "cuttlefish/io/string.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

static constexpr uint64_t kMinReadSize = 1 << 26;

struct LazilyLoadedFile::Impl {
  ~Impl();

  std::string MetadataFile() const;
  Result<void> ReadMetadata();
  Result<void> WriteMetadata();

  Result<uint64_t, int> PRead(void*, uint64_t, uint64_t);

  std::string filename_;
  Fd contents_file_;
  std::unique_ptr<ReaderSeeker> callback_;
  DisjointRangeSet already_downloaded_;
  uint64_t size_;
  std::vector<char> extended_read_buffer_;
  std::mutex mutex_;
};

Result<LazilyLoadedFile> LazilyLoadedFile::Create(
    std::string filename, uint64_t size,
    std::unique_ptr<ReaderSeeker> backing) {
  CF_EXPECT(backing.get());
  std::unique_ptr<Impl> impl = std::make_unique<Impl>();
  CF_EXPECT(impl.get());

  impl->contents_file_ = CF_EXPECT(Fd::Open(filename, O_CREAT | O_RDWR, 0644));
  impl->filename_ = std::move(filename);
  impl->callback_ = std::move(backing);
  impl->size_ = size;
  impl->extended_read_buffer_ = std::vector<char>(kMinReadSize);

  CF_EXPECT(impl->ReadMetadata());

  return LazilyLoadedFile(std::move(impl));
}

LazilyLoadedFile::LazilyLoadedFile(std::unique_ptr<Impl> impl)
    : ReaderFakeSeeker(impl->size_), impl_(std::move(impl)) {}

LazilyLoadedFile::LazilyLoadedFile(LazilyLoadedFile&&) = default;

LazilyLoadedFile& LazilyLoadedFile::operator=(LazilyLoadedFile&&) = default;

LazilyLoadedFile::~LazilyLoadedFile() = default;

Result<void> LazilyLoadedFile::Visit(IoVisitor& visitor) {
  CF_EXPECT(visitor.Accept(*this));
  return {};
}

Result<uint64_t, int> LazilyLoadedFile::PRead(void* buf, uint64_t count,
                                              uint64_t offset) const {
  CF_EXPECTV(impl_.get(), EBADF, "missing implementation");

  return CF_EXPECT(impl_->PRead(buf, count, offset));
}

LazilyLoadedFile::Impl::~Impl() {
  Result<void> res = WriteMetadata();
  if (!res.has_value()) {
    LOG(WARNING) << "fragment update failure: " << res.error();
  }
}

std::string LazilyLoadedFile::Impl::MetadataFile() const {
  return filename_ + ".frag_data";
}

Result<void> LazilyLoadedFile::Impl::ReadMetadata() {
  Fd metadata_fd = CF_EXPECT(Fd::Open(MetadataFile(), O_CREAT | O_RDWR, 0644));

  const std::string data = CF_EXPECT(ReadToString(metadata_fd));

  Result<DisjointRangeSet> parsed_res = DeserializeDisjointRangeSet(data);
  if (parsed_res.has_value()) {
    already_downloaded_ = std::move(*parsed_res);
  } else {
    LOG(WARNING) << "Invalid fragments: " << parsed_res.error();
  }

  return {};
}

Result<void> LazilyLoadedFile::Impl::WriteMetadata() {
  if (filename_.empty() || already_downloaded_.AllRanges().empty()) {
    return {};
  }

  std::pair<Fd, std::string> fd_name =
      CF_EXPECT(Fd::Mkostemp(MetadataFile() + "."));
  CF_EXPECT(fd_name.first.Chmod(0644));

  const std::string data = Serialize(already_downloaded_);
  CF_EXPECT(PWriteExact(fd_name.first, data, 0));

  CF_EXPECT(RenameFile(fd_name.second, MetadataFile()));

  return {};
}

Result<uint64_t, int> LazilyLoadedFile::Impl::PRead(void* data, uint64_t size,
                                                    uint64_t offset) {
  if (size == 0 || offset >= size_) {
    return 0;
  }
  size = std::min(size, size_ - offset);
  const std::lock_guard lock(mutex_);

  VLOG(1) << "Reading " << size << ", seek pos " << offset;
  for (const auto& range : already_downloaded_.AllRanges()) {
    VLOG(1) << "Already downloaded: [" << range.first << ", " << range.second
            << ")";
  }
  const std::optional<uint64_t> end_of_present_data =
      already_downloaded_.EndOfContainingRange(offset);
  // In terms of IO performance, this aims to minimize round trips over
  // minimizing bandwidth usage.
  if (end_of_present_data.has_value()) {
    const uint64_t read_request =
        std::min<uint64_t>(*end_of_present_data - offset, size);
    const uint64_t data_read =
        CF_EXPECT(contents_file_.PRead(data, read_request, offset));
    VLOG(1) << "Read " << data_read << " from local storage, seek pos was "
            << offset;
    return data_read;
  }
  if (size < kMinReadSize) {
    const uint64_t extended_read_size = std::min(kMinReadSize, size_ - offset);
    VLOG(1) << "Extending read request from " << size << " to "
            << extended_read_size;
    const uint64_t data_read = CF_EXPECT(callback_->PRead(
        extended_read_buffer_.data(), extended_read_size, offset));
    CF_EXPECT(PWriteExact(contents_file_, extended_read_buffer_.data(),
                          data_read, offset));
    already_downloaded_.InsertRange(offset, offset + data_read);
    VLOG(1) << "Read " << data_read << " from source, seek pos was " << offset;
    const uint64_t reported_size = std::min(data_read, size);
    memcpy(data, extended_read_buffer_.data(), reported_size);
    return reported_size;
  } else {
    VLOG(1) << "Passing down read request of " << size;
    const uint64_t data_read = CF_EXPECT(callback_->PRead(data, size, offset));
    CF_EXPECT(PWriteExact(contents_file_, data, data_read, offset));
    already_downloaded_.InsertRange(offset, offset + data_read);
    VLOG(1) << "Read " << data_read << " from source, seek pos was " << offset;
    return data_read;
  }
}

}  // namespace cuttlefish
