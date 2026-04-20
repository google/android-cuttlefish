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

#pragma once

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "cuttlefish/io/filesystem.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

// CpioReader provides read-only access to files contained within a CPIO
// archive. It supports the SVR4 (newc), POSIX (odc), and binary (bin) CPIO
// formats.
class CpioReader : public ReadFilesystem {
 public:
  static Result<std::unique_ptr<CpioReader>> Open(
      std::unique_ptr<ReaderSeeker> reader);

  Result<std::unique_ptr<ReaderSeeker>> OpenReadOnly(
      std::string_view path) override;

  Result<uint32_t> FileAttributes(std::string_view path) const override;

 private:
  struct FileEntry {
    uint64_t offset;
    uint64_t size;
    uint32_t mode;
  };

  using EntriesMap = std::map<std::string, FileEntry, std::less<>>;

  CpioReader(std::unique_ptr<ReaderSeeker> reader, EntriesMap entries);

  static Result<EntriesMap> Parse(const ReaderSeeker& reader);
  static Result<EntriesMap> ParseOdc(const ReaderSeeker& reader);
  static Result<EntriesMap> ParseNewc(const ReaderSeeker& reader);
  static Result<EntriesMap> ParseBin(const ReaderSeeker& reader,
                                     bool file_is_big_endian);

  std::unique_ptr<ReaderSeeker> reader_;
  EntriesMap entries_;
};

}  // namespace cuttlefish
