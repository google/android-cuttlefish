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

#include <memory>

#include "cuttlefish/io/io.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

// Handles the LZ4 Legacy frame format, used by the linux kernel.
//
// https://github.com/lz4/lz4/blob/5c4c1fb2354133e1f3b087a341576985f8114bd5/doc/lz4_Frame_format.md#legacy-frame

Result<std::unique_ptr<Reader>> Lz4LegacyReader(std::unique_ptr<Reader>);

}  // namespace cuttlefish
