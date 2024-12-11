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

#include "host/commands/cvd/cli/commands/server_handler.h"

#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/cli/command_request.h"

namespace cuttlefish {

Result<bool> CvdServerHandler::CanHandle(const CommandRequest& request) const {
  return Contains(CmdList(), request.Subcommand());
}

Result<void> CvdServerHandler::HandleVoid(const CommandRequest& request) {
  cvd::Response response = CF_EXPECT(Handle(request));
  CF_EXPECT_EQ(response.status().code(), cvd::Status::OK,
               response.status().message());
  return {};
}

Result<cvd::Response> CvdServerHandler::Handle(const CommandRequest& request) {
  CF_EXPECT(HandleVoid(request));

  cvd::Response response;
  response.mutable_command_response();
  response.mutable_status()->set_code(cvd::Status::OK);

  return response;
}

}  // namespace cuttlefish
