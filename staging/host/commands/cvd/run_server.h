/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <optional>
#include <string>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

// names of the flags to start cvd server
inline constexpr char kInternalServerFd[] = "INTERNAL_server_fd";
inline constexpr char kInternalCarryoverClientFd[] =
    "INTERNAL_carryover_client_fd";
inline constexpr char kInternalMemoryCarryoverFd[] =
    "INTERNAL_memory_carryover_fd";
inline constexpr char kInternalAcloudTranslatorOptOut[] =
    "INTERNAL_acloud_translator_optout";
inline constexpr char kInternalRestartedInProcess[] =
    "INTERNAL_restarted_in_process";

bool IsServerModeExpected(const std::string& exec_file);

struct RunServerParam {
  SharedFD internal_server_fd;
  SharedFD carryover_client_fd;
  std::optional<SharedFD> memory_carryover_fd;
  std::optional<android::base::LogSeverity> verbosity_level;
  std::optional<bool> acloud_translator_optout;
  bool restarted_in_process;
};
Result<void> RunServer(const RunServerParam& params);

struct ParseResult {
  SharedFD internal_server_fd;
  SharedFD carryover_client_fd;
  std::optional<SharedFD> memory_carryover_fd;
  std::optional<bool> acloud_translator_optout;
  std::optional<android::base::LogSeverity> verbosity_level;
  bool restarted_in_process;
};
Result<ParseResult> ParseIfServer(cvd_common::Args& all_args);

}  // namespace cuttlefish
