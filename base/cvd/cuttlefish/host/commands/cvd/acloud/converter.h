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

#include <string>
#include <vector>

#include "host/commands/cvd/command_request.h"

#include "common/libs/utils/result.h"

namespace cuttlefish {

struct ConvertedAcloudCreateCommand {
  std::vector<CommandRequest> prep_requests;
  CommandRequest start_request;
  std::string fetch_command_str;
  std::string fetch_cvd_args_file;
  bool verbose;
};

namespace acloud_impl {

Result<ConvertedAcloudCreateCommand> ConvertAcloudCreate(
    const CommandRequest& request);

}  // namespace acloud_impl
}  // namespace cuttlefish
