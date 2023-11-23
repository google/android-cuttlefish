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

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/client.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/fetch/fetch_cvd.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/frontline_parser.h"
#include "host/commands/cvd/metrics/cvd_metrics_api.h"
#include "host/commands/cvd/run_server.h"
#include "host/commands/cvd/server_constants.h"
#include "host/libs/config/host_tools_version.h"

namespace cuttlefish {
namespace {

/**
 * Returns --verbosity value if ever exist in the entire commandline args
 *
 * Note that this will also pick up from the subtool arguments:
 *  e.g. cvd start --verbosity=DEBUG
 *
 * This may be incorrect as the verbosity should be ideally applied to the
 * launch_cvd/cvd_internal_start only.
 *
 * However, parsing the --verbosity flag only from the driver is quite
 * complicated as we do not know the full list of the subcommands,
 * the subcommands flags, and even the selector/driver flags.
 *
 * Thus, we live with the corner case for now.
 */
android::base::LogSeverity CvdVerbosityOption(const int argc, char** argv) {
  cvd_common::Args all_args = ArgsToVec(argc, argv);
  std::string verbosity_flag_value;
  std::vector<Flag> verbosity_flag{
      GflagsCompatFlag("verbosity", verbosity_flag_value)};
  if (!ParseFlags(verbosity_flag, all_args).ok()) {
    LOG(ERROR) << "Verbosity flag parsing failed, so use the default value.";
    return GetMinimumVerbosity();
  }
  if (verbosity_flag_value.empty()) {
    return GetMinimumVerbosity();
  }
  auto encoded_verbosity = EncodeVerbosity(verbosity_flag_value);
  return (encoded_verbosity.ok() ? *encoded_verbosity : GetMinimumVerbosity());
}

/**
 * Terminates a cvd server listening on "cvd_server"
 *
 * So far, the server processes across users were listing on the "cvd_server"
 * socket. And, so far, we had one user. Now, we have multiple users. Each
 * server listens to cvd_server_<uid>. The thing is if there is a server process
 * started out of an old executable it will be listening to "cvd_server," and
 * thus we should kill the server process first.
 */
Result<void> KillOldServer() {
  CvdClient client_to_old_server(kCvdDefaultVerbosity, "cvd_server");
  auto result = client_to_old_server.StopCvdServer(/*clear=*/true);
  if (!result.ok()) {
    LOG(ERROR) << "Old server listening on \"cvd_server\" socket "
               << "must be killed first but failed to terminate it.";
    LOG(ERROR) << "Perhaps, try cvd reset -y";
    CF_EXPECT(std::move(result));
  }
  return {};
}

Result<void> CvdMain(int argc, char** argv, char** envp,
                     const android::base::LogSeverity verbosity) {
  CF_EXPECT(KillOldServer());

  cvd_common::Args all_args = ArgsToVec(argc, argv);
  CF_EXPECT(!all_args.empty());

  auto env = EnvpToMap(envp);
  CvdMetrics::SendCvdMetrics(all_args);

  if (android::base::Basename(all_args[0]) == "fetch_cvd") {
    CF_EXPECT(FetchCvdMain(argc, argv));
    return {};
  }

  CvdClient client(verbosity);

  // TODO(b/206893146): Make this decision inside the server.
  if (android::base::Basename(all_args[0]) == "acloud") {
    return client.HandleAcloud(all_args, env);
  }

  if (IsServerModeExpected(all_args[0])) {
    auto parsed = CF_EXPECT(ParseIfServer(all_args));

    return RunServer(
        {.internal_server_fd = parsed.internal_server_fd,
         .carryover_client_fd = parsed.carryover_client_fd,
         .memory_carryover_fd = parsed.memory_carryover_fd,
         .verbosity_level = parsed.verbosity_level,
         .acloud_translator_optout = parsed.acloud_translator_optout});
  }

  if (android::base::Basename(all_args[0]) == "cvd") {
    CF_EXPECT(client.HandleCvdCommand(all_args, env));
    return {};
  }

  CF_EXPECT(client.ValidateServerVersion(),
            "Unable to ensure cvd_server is running.");
  CF_EXPECT(client.HandleCommand(all_args, env, {}));

  return {};
}

}  // namespace
}  // namespace cuttlefish

int main(int argc, char** argv, char** envp) {
  android::base::LogSeverity verbosity =
      cuttlefish::CvdVerbosityOption(argc, argv);
  android::base::InitLogging(argv, android::base::StderrLogger);
  // set verbosity for this process
  cuttlefish::SetMinimumVerbosity(verbosity);

  auto result = cuttlefish::CvdMain(argc, argv, envp, verbosity);
  if (result.ok()) {
    return 0;
  } else {
    std::cerr << result.error().FormatForEnv() << std::endl;
    return -1;
  }
}
