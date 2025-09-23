/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include "cuttlefish/host/libs/command_util/runner/defs.h"

#include <ostream>

namespace cuttlefish {

std::ostream& operator<<(std::ostream& out, LauncherResponse response) {
  switch (response) {
    case LauncherResponse::kSuccess:
      return out << "LauncherResponse::kSuccess";
    case LauncherResponse::kError:
      return out << "LauncherResponse::kError";
    case LauncherResponse::kUnknownAction:
      return out << "LauncherResponse::kUnknownAction";
    default:
      int response_int = static_cast<int>(response);
      return out << "LauncherResponse::(unknown: " << response_int << ")";
  }
}

}  // namespace cuttlefish
