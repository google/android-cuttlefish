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

#include <cstdint>
#include <string>
#include <vector>

#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"

namespace cuttlefish {

class Parser {
 public:
  static Result<Parser> ConsumeAndParse(std::vector<std::string>&);

  bool IgnoreSigtstp() const;
  bool WhenDumped() const;
  bool WhenKilled() const;
  bool WhenExitedWithFailure() const;
  std::int32_t WhenExitedWithCode() const;

 private:
  Parser();

  Flag IgnoreSigtstpFlag();
  Flag WhenDumpedFlag();
  Flag WhenKilledFlag();
  Flag WhenExitedWithFailureFlag();
  Flag WhenExitedWithCodeFlag();

  bool ignore_sigtstp_;
  bool when_dumped_;
  bool when_killed_;
  bool when_exited_with_failure_;
  std::int32_t when_exited_with_code_;
};

}  // namespace cuttlefish
