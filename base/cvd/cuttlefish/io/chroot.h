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

#include "cuttlefish/io/filesystem.h"

#include <stdint.h>

#include <string>
#include <string_view>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

/**
 * Wraps access to another ReadWriteFilesystem with an imposed path prefix.
 *
 * Attempts to normalize file paths to avoid escaping by using absolute paths or
 * ".." members, but is not safe against symlinks or bind mounts.
 *
 * This is comparable to the `fakechroot` tool.
 */
class ChrootReadWriteFilesystem : public ReadWriteFilesystem {
 public:
  ChrootReadWriteFilesystem(ReadWriteFilesystem& real_filesystem,
                            std::string_view path_prefix);

  Result<std::unique_ptr<ReaderSeeker>> OpenReadOnly(
      std::string_view path) override;

  Result<uint32_t> FileAttributes(std::string_view path) const override;

  Result<std::unique_ptr<ReaderWriterSeeker>> CreateFile(
      std::string_view path) override;

  Result<void> DeleteFile(std::string_view path) override;

  Result<std::unique_ptr<ReaderWriterSeeker>> OpenReadWrite(
      std::string_view path) override;

 private:
  Result<std::string> ChrootToRealPath(std::string_view) const;

  ReadWriteFilesystem* real_filesystem_;
  std::string path_prefix_;
};

}  // namespace cuttlefish
