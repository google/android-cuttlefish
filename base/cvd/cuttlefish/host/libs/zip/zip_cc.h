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

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

/* Callback interface to provide file data to libzip. */
class SeekableZipSourceCallback {
 public:
  virtual ~SeekableZipSourceCallback() = default;

  virtual bool Close() = 0;
  virtual bool Open() = 0;
  virtual int64_t Read(char* data, uint64_t len) = 0;
  virtual bool SetOffset(int64_t offset) = 0;
  virtual int64_t Offset() = 0;
  virtual uint64_t Size() = 0;
};

enum class ZipCompression {
  kDefault,
  kStore,
  kBzip2,
  kDeflate,
  kXz,
  kZstd,
};

struct ZipStat {
  std::optional<std::string> name;
  std::optional<uint64_t> index;
  std::optional<uint64_t> size;
  std::optional<uint64_t> compressed_size;
  std::optional<ZipCompression> compression_method;
};

/* A read-only or read-write storage for data.
 *
 * Can be used to provide data to libzip with the `From` methods, and can also
 * be created to retrieve data out of libzip using `Zip::GetFile`.
 *
 * No methods to write data directly are implemented, but data can be written
 * using `Zip::CreateFromSource` and `AddFile`.
 */
class ZipSource {
 public:
  friend class Zip;
  friend class ZipSourceReader;

  /* Read-only data access based on calling callback methods. */
  static Result<ZipSource> FromCallbacks(
      std::unique_ptr<SeekableZipSourceCallback>);
  /* Read-write data access based on a copy of `data` up to `size`. Does not
   * hold a reference to `data`. */
  static Result<ZipSource> FromData(const void* data, size_t size);
  /* Read-write data access to a file on disk. */
  static Result<ZipSource> FromFile(const std::string& path);
  /* Read-write data access to an in-memory buffer based on serializing a zip
   * archive. */
  static Result<ZipSource> FromZip(class Zip);

  ZipSource(ZipSource&&);
  ~ZipSource();
  ZipSource& operator=(ZipSource&&);

  Result<ZipStat> Stat();

  /* Returns a RAII instance that puts this instance in an "open for reading"
   * state. Can fail. */
  Result<class ZipSourceReader> Reader();

 private:
  struct Impl;  // For pimpl: to avoid exposing libzip headers

  ZipSource(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

/* A `ZipSource` in an "open for reading" state. */
class ZipSourceReader {
 public:
  friend class ZipSource;

  ZipSourceReader(ZipSourceReader&&);
  ~ZipSourceReader();
  ZipSourceReader& operator=(ZipSourceReader&&);

  /* Returns a failed Result on error, or a successful result with bytes read or
   * 0 on EOF. */
  Result<uint64_t> Read(void* data, uint64_t length);

  Result<void> SeekFromStart(int64_t offset);

 private:
  ZipSourceReader(ZipSource*);

  ZipSource* source_;
};

class Zip {
 public:
  friend class ZipSource;

  static Result<Zip> CreateFromSource(ZipSource);
  static Result<Zip> OpenFromSource(ZipSource);

  Zip(Zip&&);
  ~Zip();

  /* Counts members, including un-finalized ones from AddFile. */
  Result<uint64_t> NumEntries();

  /* Mutates the archive to add a file. Reading the contents of the sources that
   * are added is deferred until `Finalize` time. */
  Result<void> AddFile(const std::string& name, ZipSource);
  /* Returns a read-only zip source to decompress and extract a file from the
   * archive. */
  Result<ZipSource> GetFile(const std::string& name);
  Result<ZipSource> GetFile(uint64_t index);

  /* Performs transfers from the input `ZipSource`s to the output `ZipSource`
   * and does the archive encoding. */
  static Result<void> Finalize(Zip);

 private:
  struct Impl;  // For pimpl: to avoid exposing libzip headers

  Zip(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

}  // namespace cuttlefish
