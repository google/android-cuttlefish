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

#include <string>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

namespace cuttlefish {

Result<ReadableZip> ZipOpenRead(const std::string& fs_path);
Result<WritableZip> ZipOpenReadWrite(const std::string& fs_path);

Result<void> AddFile(WritableZip& zip, const std::string& fs_path);
Result<void> AddFileAt(WritableZip& zip, const std::string& fs_path,
                       const std::string& zip_path);

Result<void> ExtractFile(ReadableZip& zip, const std::string& zip_path,
                         const std::string& host_path);

}  // namespace cuttlefish
