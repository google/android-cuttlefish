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

#include "cuttlefish/host/libs/zip/zip_cc.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <zip.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {
namespace {

struct ZipDeleter {
  void operator()(zip_error_t* error) {
    zip_error_fini(error);
    delete error;
  }
  void operator()(zip_source_t* source) { zip_source_free(source); }
  void operator()(zip_t* zip_ptr) { zip_discard(zip_ptr); }
};

using ManagedZip = std::unique_ptr<zip_t, ZipDeleter>;
using ManagedZipError = std::unique_ptr<zip_error_t, ZipDeleter>;
using ManagedZipSource = std::unique_ptr<zip_source_t, ZipDeleter>;

ManagedZipError NewZipError() {
  ManagedZipError error(new zip_error_t);
  zip_error_init(error.get());
  return error;
}

std::string ZipErrorString(zip_error_t* error) {
  return std::string(zip_error_strerror(error));
}

std::string ZipErrorString(zip_source_t* source) {
  return ZipErrorString(zip_source_error(source));
}

std::string ZipErrorString(zip_t* source) {
  return ZipErrorString(zip_get_error(source));
}

struct ReadableCallbackSource {
  ReadableCallbackSource(std::unique_ptr<ReadableZipSourceCallback> callbacks)
      : callbacks_(std::move(callbacks)), error_(NewZipError()) {}

  std::unique_ptr<ReadableZipSourceCallback> callbacks_;
  ManagedZipError error_;
};

struct SeekableCallbackSource {
  SeekableCallbackSource(std::unique_ptr<SeekableZipSourceCallback> callbacks)
      : callbacks_(std::move(callbacks)), error_(NewZipError()) {}

  std::unique_ptr<SeekableZipSourceCallback> callbacks_;
  ManagedZipError error_;
};

int64_t HandleCallback(ReadableZipSourceCallback& callbacks, zip_error_t* error,
                       void* data, uint64_t len, zip_source_cmd_t cmd) {
  switch (cmd) {
    case ZIP_SOURCE_CLOSE: {
      bool callback_res = callbacks.Close();
      if (!callback_res) {
        zip_error_set(error, ZIP_ER_CLOSE, errno);
      }
      return callback_res ? 0 : -1;
    }
    case ZIP_SOURCE_ERROR:
      return zip_error_to_data(error, data, len);
    case ZIP_SOURCE_OPEN: {
      bool callback_res = callbacks.Open();
      if (!callback_res) {
        zip_error_set(error, ZIP_ER_OPEN, errno);
      }
      return callback_res ? 0 : -1;
    }
    case ZIP_SOURCE_READ: {
      int64_t callback_res = callbacks.Read(reinterpret_cast<char*>(data), len);
      if (callback_res < 0) {
        zip_error_set(error, ZIP_ER_READ, errno);
      }
      return callback_res;
    }
    case ZIP_SOURCE_STAT: {
      zip_stat_t* stat_out = ZIP_SOURCE_GET_ARGS(zip_stat_t, data, len, error);
      zip_stat_init(stat_out);
      stat_out->valid = ZIP_STAT_SIZE;
      stat_out->size = callbacks.Size();
      return 0;
    }
    case ZIP_SOURCE_SUPPORTS:
      return zip_source_make_command_bitmap(
          ZIP_SOURCE_CLOSE, ZIP_SOURCE_ERROR, ZIP_SOURCE_FREE, ZIP_SOURCE_OPEN,
          ZIP_SOURCE_READ, ZIP_SOURCE_STAT, ZIP_SOURCE_SUPPORTS, -1);
    default:
      zip_error_set(error, ZIP_ER_OPNOTSUPP, EINVAL);
      return -1;
  }
}

int64_t HandleCallback(SeekableZipSourceCallback& callbacks, zip_error_t* error,
                       void* data, uint64_t len, zip_source_cmd_t cmd) {
  switch (cmd) {
    case ZIP_SOURCE_SEEK: {
      int64_t new_offset = zip_source_seek_compute_offset(
          callbacks.Offset(), callbacks.Size(), data, len, error);
      if (!callbacks.SetOffset(new_offset)) {
        zip_error_set(error, ZIP_ER_SEEK, errno);
        return -1;
      }
      return 0;
    }
    case ZIP_SOURCE_SUPPORTS:
      return zip_source_make_command_bitmap(
          ZIP_SOURCE_CLOSE, ZIP_SOURCE_ERROR, ZIP_SOURCE_FREE, ZIP_SOURCE_OPEN,
          ZIP_SOURCE_READ, ZIP_SOURCE_SEEK, ZIP_SOURCE_STAT,
          ZIP_SOURCE_SUPPORTS, ZIP_SOURCE_TELL, -1);
    case ZIP_SOURCE_TELL:
      return callbacks.Offset();
    default:
      return HandleCallback(static_cast<ReadableZipSourceCallback&>(callbacks),
                            error, data, len, cmd);
  }
}

// https://libzip.org/documentation/zip_source_function.html
int64_t ReadableZipSourceCallbackFn(void* userdata, void* data, uint64_t len,
                                    zip_source_cmd_t cmd) {
  ReadableCallbackSource* source =
      reinterpret_cast<ReadableCallbackSource*>(userdata);
  if (cmd == ZIP_SOURCE_FREE) {
    delete source;
    return 0;
  }
  return HandleCallback(*source->callbacks_, source->error_.get(), data, len,
                        cmd);
}

int64_t SeekableZipSourceCallbackFn(void* userdata, void* data, uint64_t len,
                                    zip_source_cmd_t cmd) {
  SeekableCallbackSource* source =
      reinterpret_cast<SeekableCallbackSource*>(userdata);
  if (cmd == ZIP_SOURCE_FREE) {
    delete source;
    return 0;
  }
  return HandleCallback(*source->callbacks_, source->error_.get(), data, len,
                        cmd);
}

std::optional<ZipCompression> CompressionFromRaw(uint16_t method) {
  switch (method) {
    case ZIP_CM_DEFAULT:
      return ZipCompression::kDefault;
    case ZIP_CM_STORE:
      return ZipCompression::kStore;
    case ZIP_CM_BZIP2:
      return ZipCompression::kBzip2;
    case ZIP_CM_DEFLATE:
      return ZipCompression::kDeflate;
    case ZIP_CM_XZ:
      return ZipCompression::kXz;
    case ZIP_CM_ZSTD:
      return ZipCompression::kZstd;
    default:
      return {};
  }
}

}  // namespace

struct ReadableZipSource::Impl {
  Impl(ManagedZipSource raw) : raw_(std::move(raw)) {}

  ManagedZipSource raw_;
};

struct ReadableZip::Impl {
  // This may not actually be writable. The Impl class is shared between
  // ReadableZip and WritableZip, and only reading methods will be called from
  // ReadableZip.
  Impl(ManagedZip raw, WritableZipSource source)
      : raw_(std::move(raw)), source_(std::move(source)) {}

  ManagedZip raw_;
  WritableZipSource source_;
};

Result<ReadableZipSource> ReadableZipSource::FromCallbacks(
    std::unique_ptr<ReadableZipSourceCallback> callbacks) {
  CF_EXPECT(callbacks.get());

  std::unique_ptr<ReadableCallbackSource> wrapped_source =
      std::make_unique<ReadableCallbackSource>(std::move(callbacks));

  ManagedZipError error = NewZipError();
  ManagedZipSource source(zip_source_function_create(
      ReadableZipSourceCallbackFn, wrapped_source.release(), error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return ReadableZipSource(std::make_unique<Impl>(std::move(source)));
}

ReadableZipSource::ReadableZipSource(ReadableZipSource&&) = default;
ReadableZipSource::~ReadableZipSource() = default;
ReadableZipSource& ReadableZipSource::operator=(ReadableZipSource&&) = default;

Result<ZipStat> ReadableZipSource::Stat() {
  CF_EXPECT(impl_.get());
  zip_source_t* raw_source = CF_EXPECT(impl_->raw_.get());

  zip_stat_t raw_stat;
  zip_stat_init(&raw_stat);

  CF_EXPECT_EQ(zip_source_stat(raw_source, &raw_stat), 0,
               ZipErrorString(raw_source));

  ZipStat ret;
  if (raw_stat.valid & ZIP_STAT_NAME) {
    ret.name = std::string(raw_stat.name);
  }
  if (raw_stat.valid & ZIP_STAT_INDEX) {
    ret.index = raw_stat.index;
  }
  if (raw_stat.valid & ZIP_STAT_SIZE) {
    ret.size = raw_stat.size;
  }
  if (raw_stat.valid & ZIP_STAT_COMP_SIZE) {
    ret.compressed_size = raw_stat.comp_size;
  }
  if (raw_stat.valid & ZIP_STAT_COMP_METHOD) {
    ret.compression_method = CompressionFromRaw(raw_stat.comp_method);
  }
  return ret;
}

Result<ZipSourceReader> ReadableZipSource::Reader() {
  CF_EXPECT(impl_.get());
  zip_source_t* raw_source = CF_EXPECT(impl_->raw_.get());

  CF_EXPECT_EQ(zip_source_open(raw_source), 0, ZipErrorString(raw_source));

  return ZipSourceReader(this);
}

ReadableZipSource::ReadableZipSource(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

Result<SeekableZipSource> SeekableZipSource::FromCallbacks(
    std::unique_ptr<SeekableZipSourceCallback> callbacks) {
  CF_EXPECT(callbacks.get());

  std::unique_ptr<SeekableCallbackSource> wrapped_source =
      std::make_unique<SeekableCallbackSource>(std::move(callbacks));

  ManagedZipError error = NewZipError();
  ManagedZipSource source(zip_source_function_create(
      SeekableZipSourceCallbackFn, wrapped_source.release(), error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return SeekableZipSource(std::make_unique<Impl>(std::move(source)));
}

SeekableZipSource::SeekableZipSource(SeekableZipSource&&) = default;
SeekableZipSource::~SeekableZipSource() = default;
SeekableZipSource& SeekableZipSource::operator=(SeekableZipSource&& other) =
    default;

Result<SeekingZipSourceReader> SeekableZipSource::Reader() {
  CF_EXPECT(impl_.get());
  zip_source_t* raw_source = CF_EXPECT(impl_->raw_.get());

  CF_EXPECT_EQ(zip_source_open(raw_source), 0, ZipErrorString(raw_source));

  return SeekingZipSourceReader(this);
}

SeekableZipSource::SeekableZipSource(std::unique_ptr<Impl> impl)
    : ReadableZipSource(std::move(impl)) {}

Result<WritableZipSource> WritableZipSource::BorrowData(const void* data,
                                                        size_t size) {
  CF_EXPECT_NE(data, nullptr);

  ManagedZipError error = NewZipError();
  ManagedZipSource source(zip_source_buffer_create(data, size, 0, error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return WritableZipSource(std::make_unique<Impl>(std::move(source)));
}

Result<WritableZipSource> WritableZipSource::FromFile(const std::string& path) {
  ManagedZipError error = NewZipError();
  ManagedZipSource source(
      zip_source_file_create(path.c_str(), 0, ZIP_LENGTH_TO_END, error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return WritableZipSource(std::make_unique<Impl>(std::move(source)));
}

WritableZipSource::WritableZipSource(WritableZipSource&&) = default;
WritableZipSource::~WritableZipSource() = default;
WritableZipSource& WritableZipSource::operator=(WritableZipSource&&) = default;
WritableZipSource::WritableZipSource(std::unique_ptr<Impl> impl)
    : SeekableZipSource(std::move(impl)) {}

Result<ZipSourceWriter> WritableZipSource::Writer() {
  CF_EXPECT(impl_.get());
  zip_source_t* raw = CF_EXPECT(impl_->raw_.get());

  CF_EXPECT_EQ(zip_source_begin_write(raw), 0, ZipErrorString(raw));

  return ZipSourceWriter(this);
}

ZipSourceReader::ZipSourceReader(ReadableZipSource* source) : source_(source) {}

ZipSourceReader::ZipSourceReader(ZipSourceReader&& other)
    : source_(other.source_) {
  other.source_ = nullptr;
}

ZipSourceReader& ZipSourceReader::operator=(ZipSourceReader&& other) {
  source_ = other.source_;
  other.source_ = nullptr;
  return *this;
}

ZipSourceReader::~ZipSourceReader() {
  if (source_ && source_->impl_ && source_->impl_->raw_) {
    zip_source_close(source_->impl_->raw_.get());
  }
}

Result<uint64_t> ZipSourceReader::Read(void* data, uint64_t length) {
  CF_EXPECT_NE(source_, nullptr);
  zip_source_t* raw_source = CF_EXPECT(source_->impl_->raw_.get());

  int64_t read_res = zip_source_read(raw_source, data, length);

  CF_EXPECTF(read_res >= 0, "Read failed: '{}'", ZipErrorString(raw_source));

  return read_res;
}

SeekingZipSourceReader::SeekingZipSourceReader(SeekingZipSourceReader&&) =
    default;
SeekingZipSourceReader::~SeekingZipSourceReader() = default;
SeekingZipSourceReader& SeekingZipSourceReader::operator=(
    SeekingZipSourceReader&&) = default;

Result<void> SeekingZipSourceReader::SeekFromStart(int64_t offset) {
  CF_EXPECT_NE(source_, nullptr);
  CF_EXPECT(source_->impl_.get());
  zip_source_t* raw_source = CF_EXPECT(source_->impl_->raw_.get());

  CF_EXPECT_EQ(zip_source_seek(raw_source, offset, SEEK_SET), 0,
               ZipErrorString(raw_source));

  return {};
}

SeekingZipSourceReader::SeekingZipSourceReader(SeekableZipSource* ptr)
    : ZipSourceReader(ptr) {}

ZipSourceWriter::ZipSourceWriter(WritableZipSource* source) : source_(source) {}

ZipSourceWriter::ZipSourceWriter(ZipSourceWriter&& other)
    : source_(other.source_) {
  other.source_ = nullptr;
}

ZipSourceWriter& ZipSourceWriter::operator=(ZipSourceWriter&& other) {
  source_ = other.source_;
  other.source_ = nullptr;
  return *this;
}

ZipSourceWriter::~ZipSourceWriter() {
  if (source_ && source_->impl_ && source_->impl_->raw_) {
    zip_source_rollback_write(source_->impl_->raw_.get());
  }
}

Result<uint64_t> ZipSourceWriter::Write(void* data, uint64_t length) {
  CF_EXPECT_NE(data, nullptr);
  CF_EXPECT_NE(source_, nullptr);
  CF_EXPECT(source_->impl_.get());
  zip_source_t* raw_source = CF_EXPECT(source_->impl_->raw_.get());

  int64_t written = zip_source_write(raw_source, data, length);
  CF_EXPECT_GE(written, 0, ZipErrorString(raw_source));
  return static_cast<uint64_t>(written);
}

Result<void> ZipSourceWriter::SeekFromStart(int64_t offset) {
  CF_EXPECT_NE(source_, nullptr);
  CF_EXPECT(source_->impl_.get());
  zip_source_t* raw_source = CF_EXPECT(source_->impl_->raw_.get());

  CF_EXPECT_EQ(zip_source_seek_write(raw_source, offset, SEEK_SET), 0,
               ZipErrorString(raw_source));

  return {};
}

Result<void> ZipSourceWriter::Finalize(ZipSourceWriter writer) {
  CF_EXPECT_NE(writer.source_, nullptr);
  CF_EXPECT(writer.source_->impl_.get());
  zip_source_t* raw = CF_EXPECT(writer.source_->impl_->raw_.get());

  CF_EXPECT_EQ(zip_source_commit_write(raw), 0, ZipErrorString(raw));

  return {};
}

Result<ReadableZip> ReadableZip::FromSource(SeekableZipSource source) {
  zip_source_t* source_raw = CF_EXPECT(source.impl_->raw_.get());

  ManagedZipError error = NewZipError();
  zip_source_keep(source_raw);

  ManagedZip zip_ret(zip_open_from_source(source_raw, 0, error.get()));

  if (!zip_ret.get()) {
    zip_source_free(source_raw);  // balance zip_source_keep
    return CF_ERR(ZipErrorString(error.get()));
  }

  // The Impl type is shared between ReadableZip and WritableZip so it uses a
  // WritableZipSource type. ReadableZip won't actually call any mutating
  // methods on it.
  WritableZipSource fake_writable_source(std::move(source.impl_));

  return ReadableZip(std::make_unique<Impl>(std::move(zip_ret),
                                            std::move(fake_writable_source)));
}

ReadableZip::ReadableZip(ReadableZip&&) = default;
ReadableZip::~ReadableZip() = default;
ReadableZip& ReadableZip::operator=(ReadableZip&&) = default;

Result<uint64_t> ReadableZip::NumEntries() {
  CF_EXPECT(impl_.get());
  zip_t* raw_zip = CF_EXPECT(impl_->raw_.get());

  int64_t entries = zip_get_num_entries(raw_zip, 0);
  CF_EXPECT_GE(entries, 0, ZipErrorString(raw_zip));

  return entries;
}

Result<SeekableZipSource> ReadableZip::GetFile(const std::string& name) {
  CF_EXPECT(impl_.get());
  zip_t* raw_zip = CF_EXPECT(impl_->raw_.get());

  int64_t index = zip_name_locate(raw_zip, name.c_str(), 0);
  CF_EXPECT_GE(index, 0, ZipErrorString(raw_zip));

  return CF_EXPECT(GetFile(index));
}

Result<SeekableZipSource> ReadableZip::GetFile(uint64_t index) {
  CF_EXPECT(impl_.get());
  zip_t* raw_zip = CF_EXPECT(impl_->raw_.get());

  ManagedZipError error = NewZipError();
  ManagedZipSource raw_source(zip_source_zip_file_create(
      raw_zip, index, 0, 0, -1, nullptr, error.get()));

  CF_EXPECT(raw_source.get(), ZipErrorString(error.get()));

  return SeekableZipSource(
      std::make_unique<ReadableZipSource::Impl>(std::move(raw_source)));
}

ReadableZip::ReadableZip(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Result<WritableZip> WritableZip::FromSource(
    WritableZipSource source, WritableZip::OpenBehavior open_behavior) {
  int flags = 0;
  switch (open_behavior) {
    case OpenBehavior::KeepIfExists:
      flags = ZIP_CREATE;
      break;
    case OpenBehavior::Truncate:
      flags = ZIP_CREATE | ZIP_TRUNCATE;
      break;
  }
  return CF_EXPECT(FromSource(std::move(source), flags));
}

Result<WritableZip> WritableZip::FromSource(WritableZipSource source,
                                            int flags) {
  CF_EXPECT(source.impl_.get());
  zip_source_t* source_raw = CF_EXPECT(source.impl_->raw_.get());

  ManagedZipError error = NewZipError();
  zip_source_keep(source_raw);

  ManagedZip zip_ret(zip_open_from_source(source_raw, flags, error.get()));

  if (!zip_ret.get()) {
    zip_source_free(source_raw);  // balance zip_source_keep
    return CF_ERR(ZipErrorString(error.get()));
  }

  return WritableZip(
      std::make_unique<Impl>(std::move(zip_ret), std::move(source)));
}

WritableZip::WritableZip(WritableZip&&) = default;
WritableZip::~WritableZip() = default;
WritableZip& WritableZip::operator=(WritableZip&&) = default;

Result<void> WritableZip::AddFile(const std::string& name,
                                  ReadableZipSource source) {
  CF_EXPECT(impl_.get());
  zip_t* raw_zip = CF_EXPECT(impl_->raw_.get());

  CF_EXPECT(source.impl_.get());
  zip_source_t* raw_source = CF_EXPECT(source.impl_->raw_.get());

  CF_EXPECT_GE(zip_file_add(raw_zip, name.c_str(), raw_source, 0), 0,
               ZipErrorString(raw_zip));

  source.impl_->raw_.release();

  return {};
}

Result<void> WritableZip::Finalize(WritableZip zip_cc) {
  CF_EXPECT(zip_cc.impl_.get());
  zip_t* raw_zip = CF_EXPECT(zip_cc.impl_->raw_.get());

  CF_EXPECT_EQ(zip_close(raw_zip), 0, ZipErrorString(raw_zip));

  zip_cc.impl_->raw_.release();  // Deleted by zip_close

  return {};
}

Result<WritableZipSource> WritableZipSource::FromZip(WritableZip zip_cc) {
  CF_EXPECT(zip_cc.impl_.get());

  WritableZipSource source = std::move(zip_cc.impl_->source_);

  CF_EXPECT(WritableZip::Finalize(std::move(zip_cc)));

  return source;
}

WritableZip::WritableZip(std::unique_ptr<Impl> impl)
    : ReadableZip(std::move(impl)) {}

}  // namespace cuttlefish
