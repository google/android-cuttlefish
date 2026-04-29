/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace cuttlefish {

extern const char kTracingSocketPathEnv[];

static constexpr const uint64_t kMaxTracePacketSize = 65536;

uint64_t GetProcessId();
std::string_view GetProcessName();

uint64_t GetThreadId();
std::string_view GetThreadName();

}  // namespace cuttlefish