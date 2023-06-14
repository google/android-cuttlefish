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

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <android-base/logging.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {

enum class SnapshotCmd : int {
  kUnknown = 0,
  kSuspend = 1,
  kResume = 2,
  kSnapshotTake = 3,
};

std::ostream& operator<<(std::ostream& out, const SnapshotCmd& cmd);

struct Parsed {
  SnapshotCmd cmd;
  int instance_num;
  int wait_for_launcher;
  std::optional<android::base::LogSeverity> verbosity_level;
};
Result<Parsed> Parse(int argc, char** argv);
Result<Parsed> Parse(std::vector<std::string>& args);

}  // namespace cuttlefish
