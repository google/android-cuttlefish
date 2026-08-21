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

#include <string>
#include <string_view>

#include "cuttlefish/result/result.h"

namespace cuttlefish {

// The SHA-256 of the contents of `path`, in lowercase hexadecimal.
Result<std::string> Sha256File(const std::string& path);

// Fails unless `path` holds the hexadecimal SHA-256 `expected`, which is
// compared without regard to case. `artifact_name` names the file in the error.
Result<void> VerifySha256(const std::string& path, std::string_view expected,
                          std::string_view artifact_name);

// The same against the base64 MD5 that Cloud Storage reports for an object.
Result<void> VerifyMd5(const std::string& path, std::string_view expected,
                       std::string_view artifact_name);

}  // namespace cuttlefish
