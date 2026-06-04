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

#include "cuttlefish/host/libs/tracing/tracing_utils.h"

#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "absl/base/no_destructor.h"
#include "absl/strings/str_format.h"

#include "cuttlefish/common/libs/utils/files.h"

namespace cuttlefish {

constexpr char kTracingSocketPathEnv[] = "CUTTLEFISH_TRACING_SOCKET_PATH";

uint64_t GetProcessId() { return getpid(); }

std::string_view GetProcessName() {
  static const absl::NoDestructor<std::string> sCached = []() {
    auto result = ReadFileContents("/proc/self/cmdline");
    if (!result.ok()) {
      return absl::NoDestructor<std::string>("");
    }
    std::string cmdline = *result;
    std::string binary_fullpath = cmdline.substr(0, cmdline.find('\0'));
    std::string binary = std::filesystem::path(binary_fullpath).filename();
    return absl::NoDestructor<std::string>(binary);
  }();
  return *sCached;
}

uint64_t GetThreadId() { return gettid(); }

std::string_view GetThreadName() {
  thread_local const absl::NoDestructor<std::string> tlCached = []() {
    const uint64_t tid = GetThreadId();

    std::string name;

    char buffer[16] = {0};
    if (pthread_getname_np(pthread_self(), buffer, sizeof(buffer)) == 0) {
      name = std::string(buffer);
    } else {
      name = "Unknown";
    }

    return absl::NoDestructor<std::string>(
        absl::StrFormat("%s (tid %d)", name, tid));
  }();
  return *tlCached;
}

}  // namespace cuttlefish