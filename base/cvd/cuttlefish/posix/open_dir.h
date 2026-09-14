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

#include <dirent.h>

#include <memory>
#include <string>
#include <string_view>

#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

struct CloseDir {
  void operator()(DIR* dir);
};

Result<std::unique_ptr<DIR, CloseDir>> OpenDir(const char*);
Result<std::unique_ptr<DIR, CloseDir>> OpenDir(const std::string&);
Result<std::unique_ptr<DIR, CloseDir>> OpenDir(std::string_view);

}  // namespace cuttlefish
