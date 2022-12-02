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

#include "host/commands/cvd/server.h"

#include <set>
#include <string>
#include <vector>

#include <fruit/fruit.h>

#include "cvd_server.pb.h"

#include "common/libs/utils/files.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server_command_fetch_impl.h"
#include "host/commands/cvd/server_command_fleet_impl.h"
#include "host/commands/cvd/server_command_generic_impl.h"
#include "host/commands/cvd/server_command_start_impl.h"

namespace cuttlefish {

CommandInvocation ParseInvocation(const cvd::Request& request) {
  CommandInvocation invocation;
  if (request.contents_case() != cvd::Request::ContentsCase::kCommandRequest) {
    return invocation;
  }
  if (request.command_request().args_size() == 0) {
    return invocation;
  }
  for (const std::string& arg : request.command_request().args()) {
    invocation.arguments.push_back(arg);
  }
  invocation.arguments[0] = cpp_basename(invocation.arguments[0]);
  if (invocation.arguments[0] == "cvd") {
    if (invocation.arguments.size() == 1) {
      // Show help if user invokes `cvd` alone.
      invocation.command = "help";
      invocation.arguments = {};
    } else {  // More arguments
      invocation.command = invocation.arguments[1];
      invocation.arguments.erase(invocation.arguments.begin());
      invocation.arguments.erase(invocation.arguments.begin());
    }
  } else {
    invocation.command = invocation.arguments[0];
    invocation.arguments.erase(invocation.arguments.begin());
  }
  return invocation;
}

fruit::Component<fruit::Required<InstanceManager>> cvdCommandComponent() {
  return fruit::createComponent()
      .addMultibinding<CvdServerHandler, cvd_cmd_impl::CvdCommandHandler>()
      .addMultibinding<CvdServerHandler, cvd_cmd_impl::CvdStartCommandHandler>()
      .addMultibinding<CvdServerHandler, cvd_cmd_impl::CvdFleetCommandHandler>()
      .addMultibinding<CvdServerHandler, cvd_cmd_impl::CvdFetchHandler>();
}

}  // namespace cuttlefish
