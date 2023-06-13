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

#include <iostream>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <fmt/core.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/commands/suspend_cvd/parse.h"
#include "host/libs/command_util/util.h"
#include "host/libs/config/cuttlefish_config.h"

namespace cuttlefish {
namespace {

Result<void> SuspendCvdMain(std::vector<std::string> args) {
  CF_EXPECT(!args.empty(), "No arguments was given");
  const auto prog_path = args.front();
  args.erase(args.begin());
  auto parsed = CF_EXPECT(Parse(args));

  const CuttlefishConfig* config =
      CF_EXPECT(CuttlefishConfig::Get(), "Failed to obtain config object");
  SharedFD monitor_socket = CF_EXPECT(GetLauncherMonitor(
      *config, parsed.instance_num, parsed.wait_for_launcher));

  LOG(INFO) << "Requesting suspend";
  CF_EXPECT(WriteLauncherActionWithData(
      monitor_socket, LauncherAction::kExtended, ExtendedActionType::kSuspend,
      std::string("suspend")));

  LauncherResponse suspend_response =
      CF_EXPECT(ReadLauncherResponse(monitor_socket));
  std::string response_error = fmt::format(
      "Received \"{}\" response from launcher monitor for suspend request",
      static_cast<char>(suspend_response));
  CF_EXPECT(suspend_response == LauncherResponse::kSuccess, response_error);
  LOG(INFO) << "Suspend was successful.";
  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  std::vector<std::string> all_args = cuttlefish::ArgsToVec(argc, argv);
  auto result = cuttlefish::SuspendCvdMain(std::move(all_args));
  if (!result.ok()) {
    LOG(ERROR) << result.error().Trace();
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
