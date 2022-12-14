/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include <unordered_map>

#include "common/libs/utils/json.h"
#include "host/commands/cvd/client.h"

namespace cuttlefish {

/* the very first command line parser
 *
 * Being aware of valid subcommands and cvd-specific commands, it will
 * separate the command line arguments into:
 *
 *  1. program path/name
 *  2. cvd-specific arguments
 *     a) selector flags
 *     b) non-selector flags
 *  3. subcommand
 *  4. subcommand arguments
 *
 * This is currently on the client side but will be moved to the server
 * side.
 */
class FrontlineParser {
 public:
  FrontlineParser(CvdClient& client,
                  const std::unordered_map<std::string, std::string>& env)
      : client_(client), envs_(env) {}

 private:
  /*
   * Returns the list of subcommands that cvd ever supports.
   *
   * The tool is for now intended to be internal to the parser that uses
   * command line arguments separator.
   *
   */
  Result<Json::Value> ListSubcommands();

  CvdClient& client_;
  const std::unordered_map<std::string, std::string> envs_;
};

}  // namespace cuttlefish
