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

#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <utility>

#include "zip.h"

#include "cuttlefish/host/libs/zip/libzip_cc/error.h"
#include "cuttlefish/host/libs/zip/libzip_cc/managed.h"
#include "cuttlefish/host/libs/zip/libzip_cc/readable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/writable_source.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<ReadableZip> ReadableZip::FromSource(SeekableZipSource source) {
  zip_source_t* source_raw = CF_EXPECT(source.raw_.get());

  ManagedZipError error = NewZipError();
  zip_source_keep(source_raw);

  ManagedZip zip_ret(zip_open_from_source(source_raw, 0, error.get()));

  if (!zip_ret.get()) {
    zip_source_free(source_raw);  // balance zip_source_keep
    return CF_ERR(ZipErrorString(error.get()));
  }

  WritableZipSource fake_writable_source(std::move(source.raw_));

  return ReadableZip(std::move(zip_ret), std::move(fake_writable_source));
}

// These have to be defined in the `.cc` file to avoid linker errors because of
// bazel weirdness around cmake files.
ReadableZip::ReadableZip(ReadableZip&&) = default;
ReadableZip::~ReadableZip() = default;
ReadableZip& ReadableZip::operator=(ReadableZip&&) = default;

Result<uint64_t> ReadableZip::NumEntries() {
  zip_t* raw_zip = CF_EXPECT(raw_.get());

  int64_t entries = zip_get_num_entries(raw_zip, 0);
  CF_EXPECT_GE(entries, 0, ZipErrorString(raw_zip));

  return entries;
}

Result<std::string> ReadableZip::EntryName(uint64_t index) {
  zip_t* raw_zip = CF_EXPECT(raw_.get());

  const char* name_cstr = zip_get_name(raw_zip, index, 0);
  CF_EXPECT_NE(name_cstr, nullptr, ZipErrorString(raw_zip));

  return std::string(name_cstr);
}

Result<uint32_t> ReadableZip::EntryAttributes(uint64_t index) {
  zip_t* raw_zip = CF_EXPECT(raw_.get());

  uint8_t opsys;
  uint32_t attributes;
  int res =
      zip_file_get_external_attributes(raw_zip, index, 0, &opsys, &attributes);
  CF_EXPECT_EQ(res, 0, ZipErrorString(raw_zip));

  // The fetcher must occasionally download archives from Android 10 or 11
  // which had incorrectly set the extents for the smaller files to DOS.
  // Don't error out for those.
  CF_EXPECT(opsys == ZIP_OPSYS_UNIX || opsys == ZIP_OPSYS_DOS);

  return attributes;
}

Result<bool> ReadableZip::EntryIsDirectory(uint64_t index) {
  const uint32_t attributes =
      CF_EXPECT(EntryAttributes(index),
                "Failed to get attributes for entry " << index);

  // See
  //  * https://cs.android.com/android/platform/superproject/main/+/main:build/soong/zip/zip.go;drc=8967d7562557001eb10e216ba7a947fb6054c67c;l=782
  //  * https://cs.android.com/android/platform/superproject/main/+/main:build/soong/third_party/zip/struct.go;drc=61197364367c9e404c7da6900658f1b16c42d0da;l=225
  const mode_t mode = (attributes >> 16);

  return S_ISDIR(mode);
}

Result<SeekableZipSource> ReadableZip::GetFile(const std::string& name) {
  zip_t* raw_zip = CF_EXPECT(raw_.get());

  int64_t index = zip_name_locate(raw_zip, name.c_str(), 0);
  CF_EXPECT_GE(index, 0, ZipErrorString(raw_zip));

  return CF_EXPECT(GetFile(index));
}

Result<SeekableZipSource> ReadableZip::GetFile(uint64_t index) {
  zip_t* raw_zip = CF_EXPECT(raw_.get());

  ManagedZipError error = NewZipError();
  ManagedZipSource raw_source(zip_source_zip_file_create(
      raw_zip, index, 0, 0, -1, nullptr, error.get()));

  CF_EXPECT(raw_source.get(), ZipErrorString(error.get()));

  return SeekableZipSource(std::move(raw_source));
}

ReadableZip::ReadableZip(ManagedZip raw, WritableZipSource source)
    : raw_(std::move(raw)), source_(std::move(source)) {}

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
  zip_source_t* source_raw = CF_EXPECT(source.raw_.get());

  ManagedZipError error = NewZipError();
  zip_source_keep(source_raw);

  ManagedZip zip_ret(zip_open_from_source(source_raw, flags, error.get()));

  if (!zip_ret.get()) {
    zip_source_free(source_raw);  // balance zip_source_keep
    return CF_ERR(ZipErrorString(error.get()));
  }

  return WritableZip(std::move(zip_ret), std::move(source));
}

Result<void> WritableZip::AddFile(const std::string& name,
                                  ReadableZipSource source) {
  zip_t* raw_zip = CF_EXPECT(raw_.get());

  zip_source_t* raw_source = CF_EXPECT(source.raw_.get());

  CF_EXPECT_GE(zip_file_add(raw_zip, name.c_str(), raw_source, 0), 0,
               ZipErrorString(raw_zip));

  source.raw_.release();

  return {};
}

Result<void> WritableZip::Finalize(WritableZip zip_cc) {
  zip_t* raw_zip = CF_EXPECT(zip_cc.raw_.get());

  CF_EXPECT_EQ(zip_close(raw_zip), 0, ZipErrorString(raw_zip));

  zip_cc.raw_.release();  // Deleted by zip_close

  return {};
}

Result<WritableZipSource> WritableZipSource::FromZip(WritableZip zip_cc) {
  WritableZipSource source = std::move(zip_cc.source_);

  CF_EXPECT(WritableZip::Finalize(std::move(zip_cc)));

  return source;
}

WritableZip::WritableZip(ManagedZip raw, WritableZipSource source)
    : ReadableZip(std::move(raw), std::move(source)) {}

}  // namespace cuttlefish
