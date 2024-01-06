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

#include "host/commands/cvd_update_security_algorithm/update_security_algorithm_command_builder.h"

#include <string>

#include <fmt/format.h>

namespace cuttlefish {

std::string GetATCommand(int32_t connection_event, int32_t encryption,
                         int32_t integrity, bool is_unprotected_emergency) {
  return fmt::format("AT+UPDATESECURITYALGORITHM:{},{},{},{:d}\r",
                     connection_event, encryption, integrity,
                     is_unprotected_emergency);
}
}  // namespace cuttlefish
