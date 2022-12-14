/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <string>
#include <vector>

#include "host/commands/cvd/selector/start_selector_parser.h"

namespace cuttlefish {
namespace selector {

struct CommandAndSelectorArguments {
  std::vector<std::string> cmd_args;
  std::vector<std::string> selector_args;
};

Result<CommandAndSelectorArguments> GetCommandAndSelectorArguments(
    const std::vector<std::string>& args);

}  // namespace selector
}  // namespace cuttlefish
