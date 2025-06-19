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

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

namespace cuttlefish {

class ZipBuilder {
 public:
  static Result<ZipBuilder> AppendingTo(WritableZip existing);
  static Result<ZipBuilder> TargetingFile(const std::string& fs_path);
  static Result<ZipBuilder> TargetingSource(WritableZipSource);

  Result<void> AddFile(const std::string& fs_path);
  Result<void> AddFileAt(const std::string& fs_path,
                         const std::string& zip_path);

  Result<void> AddDataAt(const std::string& data, const std::string& zip_path);
  Result<void> AddDataAt(const std::vector<char>& data,
                         const std::string& zip_path);
  Result<void> AddDataAt(const void* data, size_t size,
                         const std::string& zip_path);
  Result<void> AddDataAt(ReadableZipSource source, const std::string& zip_path);

  static WritableZip ToRaw(ZipBuilder);
  static Result<void> Finalize(ZipBuilder);

 private:
  ZipBuilder(WritableZip archive);

  WritableZip archive_;
};

}  // namespace cuttlefish
