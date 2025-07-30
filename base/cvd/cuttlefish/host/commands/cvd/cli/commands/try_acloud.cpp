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

#include "cuttlefish/host/commands/cvd/cli/commands/try_acloud.h"

#include <signal.h>  // IWYU pragma: keep
#include <stdlib.h>

#include <csignal>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <android-base/logging.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/host/commands/cvd/acloud/config.h"
#include "cuttlefish/host/commands/cvd/acloud/converter.h"
#include "cuttlefish/host/commands/cvd/acloud/create_converter_parser.h"
#include "cuttlefish/host/commands/cvd/cli/command_request.h"
#include "cuttlefish/host/commands/cvd/cli/commands/acloud_common.h"
#include "cuttlefish/host/commands/cvd/cli/commands/command_handler.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"

#define ENABLE_CVDR_TRANSLATION 1

namespace cuttlefish {
namespace {

constexpr char kCvdrBinName[] = "cvdr";

constexpr char kSummaryHelpText[] =
    "Test whether an `acloud CLI` command could be satisfied using either "
    "`cvd` or `cvdr`";

constexpr char kDetailedHelpText[] =
    R"(cvd try-acloud - verifies whether an original `acloud CLI` command
    could be satisfied using either:
   
    - `cvd` for local instance management, determined by flag
    `--local-instance`.
   
    - Or `cvdr` for remote instance management.)";

bool CheckIfCvdrExist() { return Execute({"which", kCvdrBinName}) == 0; }

}  // namespace

class TryAcloudCommand : public CvdCommandHandler {
 public:
  ~TryAcloudCommand() = default;

  cvd_common::Args CmdList() const override { return {"try-acloud"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

  Result<void> Handle(const CommandRequest& request) override {
#if ENABLE_CVDR_TRANSLATION
    Result<void> res = VerifyWithCvdRemote(request);
    if (res.ok()) {
      return {};
    } else {
      CF_EXPECT(VerifyWithCvd(request));
      return {};
    }
#endif
    CF_EXPECT(VerifyWithCvd(request));
    return {};
  }

 private:
  Result<void> VerifyWithCvd(const CommandRequest& request);
  Result<void> VerifyWithCvdRemote(const CommandRequest& request);
  Result<std::string> RunCvdRemoteGetConfig(const std::string& name);
};

Result<void> TryAcloudCommand::VerifyWithCvd(const CommandRequest& request) {
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(IsSubOperationSupported(request));
  // ConvertAcloudCreate converts acloud to cvd commands.
  auto converted = CF_EXPECT(acloud_impl::ConvertAcloudCreate(request));
  // currently, optout/optin feature only works in local instance
  // remote instance would continue to be done either through `python acloud` or
  // `cvdr` (if enabled).
  auto optout =
      true;  // CF_EXPECT(instance_manager_.GetAcloudTranslatorOptout());
  CF_EXPECT(!optout);
  return {};
}

Result<void> TryAcloudCommand::VerifyWithCvdRemote(
    const CommandRequest& request) {
  auto filename = CF_EXPECT(GetDefaultConfigFile());
  auto config = CF_EXPECT(LoadAcloudConfig(filename));
  CF_EXPECT(config.use_legacy_acloud == false);
  CF_EXPECT(CheckIfCvdrExist());
  std::vector<std::string> args = request.SubcommandArguments();
  CF_EXPECT(acloud_impl::CompileFromAcloudToCvdr(args));
  std::string cvdr_service_url =
      CF_EXPECT(RunCvdRemoteGetConfig("service_url"));
  CF_EXPECT(config.project == "google.com:android-treehugger-developer" &&
            cvdr_service_url ==
                "http://android-treehugger-developer.googleplex.com");
  std::string cvdr_zone = CF_EXPECT(RunCvdRemoteGetConfig("zone"));
  CF_EXPECT(config.zone == cvdr_zone);
  return {};
}

Result<std::string> TryAcloudCommand::RunCvdRemoteGetConfig(
    const std::string& name) {
  Command cmd = Command("cvdr");
  cmd.AddParameter("get_config");
  cmd.AddParameter(name);
  std::string stdout_;
  SharedFD stdout_pipe_read, stdout_pipe_write;
  CF_EXPECT(SharedFD::Pipe(&stdout_pipe_read, &stdout_pipe_write),
            "Could not create a pipe");
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, stdout_pipe_write);
  std::thread stdout_thread([stdout_pipe_read, &stdout_]() {
    int read = ReadAll(stdout_pipe_read, &stdout_);
    if (read < 0) {
      LOG(ERROR) << "Error in reading stdout from process";
    }
  });

  siginfo_t siginfo;  // NOLINT(misc-include-cleaner)
  cmd.Start().Wait(&siginfo, WEXITED);
  {
    // Force the destructor to run by moving it into a smaller scope.
    // This is necessary to close the write end of the pipe.
    Command forceDelete = std::move(cmd);
  }
  stdout_pipe_write->Close();
  stdout_thread.join();
  CF_EXPECT(siginfo.si_status == EXIT_SUCCESS);  // NOLINT(misc-include-cleaner)
  stdout_.erase(stdout_.find('\n'));
  return stdout_;
}

std::unique_ptr<CvdCommandHandler> NewTryAcloudCommand() {
  return std::unique_ptr<CvdCommandHandler>(new TryAcloudCommand());
}

}  // namespace cuttlefish
