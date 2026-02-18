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

#include "cuttlefish/io/filesystem.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

class NativeFilesystem : public ReadWriteFilesystem {
 public:
  Result<std::unique_ptr<ReaderSeeker>> OpenReadOnly(
      std::string_view path) override;

  Result<std::unique_ptr<ReaderWriterSeeker>> CreateFile(
      std::string_view path) override;

  Result<void> DeleteFile(std::string_view path) override;

  Result<std::unique_ptr<ReaderWriterSeeker>> OpenReadWrite(
      std::string_view path) override;
};

}  // namespace cuttlefish
