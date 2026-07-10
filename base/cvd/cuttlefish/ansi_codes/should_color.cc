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

#include "cuttlefish/ansi_codes/should_color.h"

#include <unistd.h>

#include "cuttlefish/common/libs/utils/environment.h"

namespace cuttlefish {
namespace {

bool ColorDefault(int fd) {
  // https://no-color.org/
  if (!StringFromEnv("NO_COLOR").value_or("").empty()) {
    return false;
  }
  // https://force-color.org/
  if (!StringFromEnv("FORCE_COLOR").value_or("").empty()) {
    return true;
  }
  return isatty(fd);
}

}  // namespace

bool ShouldColorStdout() { return ColorDefault(STDOUT_FILENO); }

bool ShouldColorStderr() { return ColorDefault(STDERR_FILENO); }

}  // namespace cuttlefish
