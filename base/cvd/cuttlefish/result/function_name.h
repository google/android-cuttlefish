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

#include <source_location>
#include <string>
#include <string_view>

namespace cuttlefish {

/* Best-effort attempt to extract the bare function name from a signature. */
std::string FunctionName(std::string function_signature);
std::string FunctionName(std::string_view function_signature);
std::string FunctionName(const char* function_signature);
std::string FunctionName(
    std::source_location location = std::source_location::current());

}  // namespace cuttlefish
