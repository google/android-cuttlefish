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

#include "common/libs/utils/result.h"
#include "host/commands/cvd/command_request.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CvdServerHandler {
 public:
  virtual ~CvdServerHandler() = default;

  virtual Result<bool> CanHandle(const CommandRequest&) const = 0;
  virtual Result<cvd::Response> Handle(const CommandRequest&) = 0;
  // returns the list of subcommand it can handle
  virtual cvd_common::Args CmdList() const = 0;
  // used for command help text
  virtual Result<std::string> SummaryHelp() const = 0;
  virtual bool ShouldInterceptHelp() const = 0;
  virtual Result<std::string> DetailedHelp(std::vector<std::string>&) const = 0;
};

}  // namespace cuttlefish
