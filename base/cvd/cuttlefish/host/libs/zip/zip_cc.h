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

class ReadableZipSourceCallback {
 public:
  virtual ~ReadableZipSourceCallback() = default;

  virtual bool Close() = 0;
  virtual bool Open() = 0;
  virtual int64_t Read(char* data, uint64_t len) = 0;
  virtual uint64_t Size() = 0;
};

/* Callback interface to provide file data to libzip. */
class SeekableZipSourceCallback : public ReadableZipSourceCallback {
 public:
  virtual ~SeekableZipSourceCallback() = default;

  virtual bool SetOffset(int64_t offset) = 0;
  virtual int64_t Offset() = 0;
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

class ReadableZipSource {
 public:
  friend class ReadableZip;
  friend class SeekableZipSource;
  friend class WritableZip;
  friend class WritableZipSource;
  friend class ZipSourceReader;
  friend class SeekingZipSourceReader;
  friend class ZipSourceWriter;

  static Result<ReadableZipSource> FromCallbacks(
      std::unique_ptr<ReadableZipSourceCallback>);

  // Can be safely called with a subclass type.
  ReadableZipSource(ReadableZipSource&&);
  virtual ~ReadableZipSource();
  // Can be safely called with a subclass type.
  ReadableZipSource& operator=(ReadableZipSource&&);

  Result<ZipStat> Stat();

  /* Returns a RAII instance that puts this instance in an "open for reading"
   * state. Can fail. Should not outlive this instance. */
  Result<class ZipSourceReader> Reader();

 private:
  struct Impl;  // For pimpl: to avoid exposing libzip headers

  ReadableZipSource(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

class SeekableZipSource : public ReadableZipSource {
 public:
  friend class ReadableZip;
  friend class WritableZipSource;

  static Result<SeekableZipSource> FromCallbacks(
      std::unique_ptr<SeekableZipSourceCallback>);

  SeekableZipSource(SeekableZipSource&&);
  ~SeekableZipSource() override;
  SeekableZipSource& operator=(SeekableZipSource&&);

  /* Returns a RAII instance that puts this instance in an "open for reading"
   * state. Can fail. Should not outlive this instance. */
  Result<class SeekingZipSourceReader> Reader();

 private:
  SeekableZipSource(std::unique_ptr<Impl>);
};

class WritableZipSource : public SeekableZipSource {
 public:
  friend class ReadableZip;
  /* References `data`, may not update it on write but `data` should outlive the
   * returned instance. */
  static Result<WritableZipSource> BorrowData(const void* data, size_t size);
  static Result<WritableZipSource> FromFile(const std::string& path);
  /* Data access to an in-memory buffer based on serializing a zip archive. */
  static Result<WritableZipSource> FromZip(class WritableZip);

  WritableZipSource(WritableZipSource&&);
  virtual ~WritableZipSource();
  WritableZipSource& operator=(WritableZipSource&&);

  /* Returns a RAII instance that puts this instance in an "open for writing"
   * state. Can fail. Should not outlive this instance. Cannot be used at the
   * same time as the `Reader()` method from superclasses. */
  Result<class ZipSourceWriter> Writer();

 private:
  WritableZipSource(std::unique_ptr<Impl>);
};

/* A `ReadableZipSource` in an "open for reading" state. */
class ZipSourceReader {
 public:
  friend class ReadableZipSource;
  friend class SeekingZipSourceReader;

  ZipSourceReader(ZipSourceReader&&);
  virtual ~ZipSourceReader();
  ZipSourceReader& operator=(ZipSourceReader&&);

  /* Returns a failed Result on error, or a successful result with bytes read or
   * 0 on EOF. */
  Result<uint64_t> Read(void* data, uint64_t length);

 private:
  ZipSourceReader(ReadableZipSource*);

  ReadableZipSource* source_;
};

/* A `SeekableZipSource` in an "open for reading" state. */
class SeekingZipSourceReader : public ZipSourceReader {
 public:
  friend class SeekableZipSource;

  SeekingZipSourceReader(SeekingZipSourceReader&&);
  ~SeekingZipSourceReader() override;
  SeekingZipSourceReader& operator=(SeekingZipSourceReader&&);

  Result<void> SeekFromStart(int64_t offset);

 private:
  SeekingZipSourceReader(SeekableZipSource*);
};

/* A `WritableZipSource` in an "open for writing" state. */
class ZipSourceWriter {
 public:
  friend class WritableZipSource;

  ZipSourceWriter(ZipSourceWriter&&);
  ~ZipSourceWriter();
  ZipSourceWriter& operator=(ZipSourceWriter&&);

  /* Writes are not committed until `Finalize` is called. Returns number of
   * bytes written. */
  Result<uint64_t> Write(void* data, uint64_t length);
  Result<void> SeekFromStart(int64_t offset);

  /* Commits writes and closes the writer. */
  static Result<void> Finalize(ZipSourceWriter);

 private:
  ZipSourceWriter(WritableZipSource*);

  WritableZipSource* source_;
};

class ReadableZip {
 public:
  friend class WritableZip;
  friend class WritableZipSource;

  static Result<ReadableZip> FromSource(SeekableZipSource);

  ReadableZip(ReadableZip&&);
  virtual ~ReadableZip();
  ReadableZip& operator=(ReadableZip&&);

  /* Counts members, including un-finalized ones from AddFile. */
  Result<uint64_t> NumEntries();

  /* Decompresses and extract a file from the archive. */
  Result<SeekableZipSource> GetFile(const std::string& name);
  Result<SeekableZipSource> GetFile(uint64_t index);

 private:
  struct Impl;  // For pimpl: to avoid exposing libzip headers

  ReadableZip(std::unique_ptr<Impl>);

  std::unique_ptr<Impl> impl_;
};

class WritableZip : public ReadableZip {
 public:
  static Result<WritableZip> FromSource(WritableZipSource);

  WritableZip(WritableZip&&);
  ~WritableZip() override;
  WritableZip& operator=(WritableZip&&);

  /* Mutates the archive to add a file. Reading the contents of the sources that
   * are added is deferred until `Finalize` time. */
  Result<void> AddFile(const std::string& name, ReadableZipSource);

  /* Performs transfers from the input `ZipSource`s to the output `ZipSource`
   * and does the archive encoding. */
  static Result<void> Finalize(WritableZip);

 private:
  WritableZip(std::unique_ptr<Impl>);
};

}  // namespace cuttlefish
