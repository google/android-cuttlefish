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

#include "cuttlefish/io/io.h"

#include <stdint.h>

#include <string_view>

#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class ReadFilesystem {
 public:
  virtual ~ReadFilesystem() = default;

  virtual Result<std::unique_ptr<ReaderSeeker>> OpenReadOnly(
      std::string_view path) = 0;
};

class ReadWriteFilesystem : public ReadFilesystem {
 public:
  virtual Result<std::unique_ptr<ReaderWriterSeeker>> CreateFile(
      std::string_view path) = 0;

  virtual Result<void> DeleteFile(std::string_view path) = 0;

  virtual Result<std::unique_ptr<ReaderWriterSeeker>> OpenReadWrite(
      std::string_view path) = 0;
};

}  // namespace cuttlefish
