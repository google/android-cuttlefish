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

struct CallbackSource {
  CallbackSource(std::unique_ptr<SeekableZipSourceCallback> callbacks)
      : callbacks_(std::move(callbacks)), error_(NewZipError()) {}

  std::unique_ptr<SeekableZipSourceCallback> callbacks_;
  ManagedZipError error_;
};

// https://libzip.org/documentation/zip_source_function.html
int64_t ZipSourceCallback(void* userdata, void* data, uint64_t len,
                          zip_source_cmd_t cmd) {
  CallbackSource* source = reinterpret_cast<CallbackSource*>(userdata);
  zip_error_t* error = source->error_.get();
  SeekableZipSourceCallback& callbacks = *source->callbacks_;
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
    case ZIP_SOURCE_FREE:
      delete source;
      return 0;
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
    case ZIP_SOURCE_SEEK: {
      int64_t new_offset = zip_source_seek_compute_offset(
          callbacks.Offset(), callbacks.Size(), data, len, error);
      if (!callbacks.SetOffset(new_offset)) {
        zip_error_set(error, ZIP_ER_SEEK, errno);
        return -1;
      }
      return 0;
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
          ZIP_SOURCE_READ, ZIP_SOURCE_SEEK, ZIP_SOURCE_STAT,
          ZIP_SOURCE_SUPPORTS, ZIP_SOURCE_TELL, -1);
    case ZIP_SOURCE_TELL:
      return callbacks.Offset();
    default:
      zip_error_set(error, ZIP_ER_OPNOTSUPP, EINVAL);
      return -1;
  }
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

struct ZipSource::Impl {
  Impl(ManagedZipSource raw) : raw_(std::move(raw)) {}

  ManagedZipSource raw_;
};

struct Zip::Impl {
  Impl(ManagedZip raw, ZipSource source)
      : raw_(std::move(raw)), source_(std::move(source)) {}

  ManagedZip raw_;
  ZipSource source_;
};

Result<ZipSource> ZipSource::FromData(const void* data, size_t size) {
  CF_EXPECT_NE(data, nullptr);

  ManagedZipError error = NewZipError();
  ManagedZipSource source(zip_source_buffer_create(data, size, 0, error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return ZipSource(std::unique_ptr<Impl>(new Impl(std::move(source))));
}

Result<ZipSource> ZipSource::FromCallbacks(
    std::unique_ptr<SeekableZipSourceCallback> callbacks) {
  CF_EXPECT(callbacks.get());

  std::unique_ptr<CallbackSource> wrapped_source =
      std::make_unique<CallbackSource>(std::move(callbacks));

  ManagedZipError error = NewZipError();
  ManagedZipSource source(zip_source_function_create(
      ZipSourceCallback, wrapped_source.release(), error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return ZipSource(std::unique_ptr<Impl>(new Impl(std::move(source))));
}

Result<ZipSource> ZipSource::FromFile(const std::string& path) {
  ManagedZipError error = NewZipError();
  ManagedZipSource source(zip_source_file_create(
      path.c_str(), 0, ZIP_LENGTH_UNCHECKED, error.get()));

  CF_EXPECT(source.get(), ZipErrorString(error.get()));

  return ZipSource(std::unique_ptr<Impl>(new Impl(std::move(source))));
}

ZipSource::ZipSource(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ZipSource::ZipSource(ZipSource&&) = default;

ZipSource::~ZipSource() = default;

ZipSource& ZipSource::operator=(ZipSource&&) = default;

Result<ZipStat> ZipSource::Stat() {
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

Result<ZipSourceReader> ZipSource::Reader() {
  CF_EXPECT(impl_.get());
  zip_source_t* raw_source = CF_EXPECT(impl_->raw_.get());

  CF_EXPECT_EQ(zip_source_open(raw_source), 0, ZipErrorString(raw_source));

  ZipSourceReader reader(this);
  CF_EXPECT(reader.SeekFromStart(0));
  return std::move(reader);
}

ZipSourceReader::ZipSourceReader(ZipSource* source) : source_(source) {}

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
  if (!source_) {
    return;
  }
  if (!source_->impl_) {
    return;
  }
  if (!source_->impl_->raw_) {
    return;
  }

  zip_source_close(source_->impl_->raw_.get());
}

Result<uint64_t> ZipSourceReader::Read(void* data, uint64_t length) {
  CF_EXPECT_NE(source_, nullptr);
  zip_source_t* raw_source = CF_EXPECT(source_->impl_->raw_.get());

  int64_t read_res = zip_source_read(raw_source, data, length);

  CF_EXPECTF(read_res >= 0, "Read failed: '{}'", ZipErrorString(raw_source));

  return read_res;
}

Result<void> ZipSourceReader::SeekFromStart(int64_t offset) {
  CF_EXPECT_NE(source_, nullptr);
  zip_source_t* raw_source = CF_EXPECT(source_->impl_->raw_.get());

  CF_EXPECT_EQ(zip_source_seek(raw_source, offset, SEEK_SET), 0,
               ZipErrorString(raw_source));

  return {};
}

Result<Zip> Zip::CreateFromSource(ZipSource source) {
  CF_EXPECT(source.impl_.get());
  zip_source_t* source_raw = CF_EXPECT(source.impl_->raw_.get());

  ManagedZipError error = NewZipError();
  zip_source_keep(source_raw);

  ManagedZip zip_ret(
      zip_open_from_source(source_raw, ZIP_CREATE | ZIP_TRUNCATE, error.get()));

  if (!zip_ret.get()) {
    zip_source_free(source_raw);  // balance zip_source_keep
    return CF_ERR(ZipErrorString(error.get()));
  }

  return Zip(std::make_unique<Impl>(std::move(zip_ret), std::move(source)));
}

Result<Zip> Zip::OpenFromSource(ZipSource source) {
  zip_source_t* source_raw = CF_EXPECT(source.impl_->raw_.get());

  ManagedZipError error = NewZipError();
  zip_source_keep(source_raw);

  ManagedZip zip_ret(zip_open_from_source(source_raw, 0, error.get()));

  if (!zip_ret.get()) {
    zip_source_free(source_raw);  // balance zip_source_keep
    return CF_ERR(ZipErrorString(error.get()));
  }

  return Zip(std::make_unique<Impl>(std::move(zip_ret), std::move(source)));
}

Zip::Zip(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Zip::Zip(Zip&&) = default;

Zip::~Zip() = default;

Result<uint64_t> Zip::NumEntries() {
  CF_EXPECT(impl_.get());
  zip_t* raw_zip = CF_EXPECT(impl_->raw_.get());

  int64_t entries = zip_get_num_entries(raw_zip, 0);
  CF_EXPECT_GE(entries, 0, ZipErrorString(raw_zip));

  return entries;
}

Result<void> Zip::AddFile(const std::string& name, ZipSource source) {
  CF_EXPECT(impl_.get());
  zip_t* raw_zip = CF_EXPECT(impl_->raw_.get());

  CF_EXPECT(source.impl_.get());
  zip_source_t* raw_source = CF_EXPECT(source.impl_->raw_.get());

  CF_EXPECT_GE(zip_file_add(raw_zip, name.c_str(), raw_source, 0), 0,
               ZipErrorString(raw_zip));

  source.impl_->raw_.release();

  return {};
}

Result<ZipSource> Zip::GetFile(const std::string& name) {
  CF_EXPECT(impl_.get());
  zip_t* raw_zip = CF_EXPECT(impl_->raw_.get());

  int64_t index = zip_name_locate(raw_zip, name.c_str(), 0);
  CF_EXPECT_GE(index, 0, ZipErrorString(raw_zip));

  return CF_EXPECT(GetFile(index));
}

Result<ZipSource> Zip::GetFile(uint64_t index) {
  CF_EXPECT(impl_.get());
  zip_t* raw_zip = CF_EXPECT(impl_->raw_.get());

  ManagedZipError error = NewZipError();
  ManagedZipSource raw_source(zip_source_zip_file_create(
      raw_zip, index, 0, 0, -1, nullptr, error.get()));

  CF_EXPECT(raw_source.get(), ZipErrorString(error.get()));

  return ZipSource(std::unique_ptr<ZipSource::Impl>(
      new ZipSource::Impl(std::move(raw_source))));
}

Result<void> Zip::Finalize(Zip zip_cc) {
  CF_EXPECT(zip_cc.impl_.get());
  zip_t* raw_zip = CF_EXPECT(zip_cc.impl_->raw_.get());

  CF_EXPECT_EQ(zip_close(raw_zip), 0, ZipErrorString(raw_zip));

  zip_cc.impl_->raw_.release();  // Deleted by zip_close

  return {};
}

Result<ZipSource> ZipSource::FromZip(Zip zip_cc) {
  CF_EXPECT(zip_cc.impl_.get());

  ZipSource source = std::move(zip_cc.impl_->source_);

  CF_EXPECT(Zip::Finalize(std::move(zip_cc)));

  return source;
}

}  // namespace cuttlefish
