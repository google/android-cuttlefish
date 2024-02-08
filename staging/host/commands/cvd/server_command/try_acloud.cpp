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

#include "host/commands/cvd/server_command/try_acloud.h"

#include <mutex>
#include <thread>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/acloud/config.h"
#include "host/commands/cvd/acloud/converter.h"
#include "host/commands/cvd/acloud/create_converter_parser.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/subprocess_waiter.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

#define ENABLE_CVDR_TRANSLATION 1

namespace cuttlefish {
namespace {

constexpr char kCvdrBinName[] = "cvdr";

bool CheckIfCvdrExist() {
  auto cmd = Command("which").AddParameter(kCvdrBinName);
  int ret = RunWithManagedStdio(std::move(cmd), nullptr, nullptr, nullptr,
                                SubprocessOptions());
  return ret == 0;
}

}  // namespace

class TryAcloudCommand : public CvdServerHandler {
 public:
  TryAcloudCommand(const std::atomic<bool>& optout) : optout_(optout) {}
  ~TryAcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return invocation.command == "try-acloud";
  }

  cvd_common::Args CmdList() const override { return {"try-acloud"}; }

  /**
   * The `try-acloud` command verifies whether an original `acloud CLI` command
   * could be satisfied using either:
   *
   * - `cvd` for local instance management, determined by flag
   * `--local-instance`.
   *
   * - Or `cvdr` for remote instance management (#if ENABLE_CVDR_TRANSLATION).
   *
   * If the test fails, the command will be handed to the `python acloud CLI`.
   *
   */
  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
#if ENABLE_CVDR_TRANSLATION
    auto res = VerifyWithCvdRemote(request);
    return res.ok() ? res : VerifyWithCvd(request);
#endif
    return VerifyWithCvd(request);
  }

  Result<void> Interrupt() override {
    std::lock_guard interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    CF_EXPECT(waiter_.Interrupt());
    return {};
  }

 private:
  Result<cvd::Response> VerifyWithCvd(const RequestWithStdio& request);
  Result<cvd::Response> VerifyWithCvdRemote(const RequestWithStdio& request);
  Result<std::string> RunCvdRemoteGetConfig(const std::string& name);

  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
  SubprocessWaiter waiter_;
  const std::atomic<bool>& optout_;
};

Result<cvd::Response> TryAcloudCommand::VerifyWithCvd(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interrupt_mutex_);
  bool lock_released = false;
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(IsSubOperationSupported(request));
  auto cb_unlock = [&lock_released, &interrupt_lock](void) -> Result<void> {
    if (!lock_released) {
      interrupt_lock.unlock();
      lock_released = true;
    }
    return {};
  };
  auto cb_lock = [&lock_released, &interrupt_lock](void) -> Result<void> {
    if (lock_released) {
      interrupt_lock.lock();
      lock_released = true;
    }
    return {};
  };
  // ConvertAcloudCreate converts acloud to cvd commands.
  // The input parameters waiter_, cb_unlock, cb_lock are.used to
  // support interrupt which have locking and unlocking functions
  auto converted = CF_EXPECT(
      acloud_impl::ConvertAcloudCreate(request, waiter_, cb_unlock, cb_lock));
  if (lock_released) {
    interrupt_lock.lock();
  }
  // currently, optout/optin feature only works in local instance
  // remote instance would continue to be done either through `python acloud` or
  // `cvdr` (if enabled).
  CF_EXPECT(!optout_);
  cvd::Response response;
  response.mutable_command_response();
  return response;
}

Result<cvd::Response> TryAcloudCommand::VerifyWithCvdRemote(
    const RequestWithStdio& request) {
  auto filename = CF_EXPECT(GetDefaultConfigFile());
  auto config = CF_EXPECT(LoadAcloudConfig(filename));
  CF_EXPECT(config.use_legacy_acloud == false);
  CF_EXPECT(CheckIfCvdrExist());
  auto args = ParseInvocation(request.Message()).arguments;
  CF_EXPECT(acloud_impl::CompileFromAcloudToCvdr(args));
  std::string cvdr_service_url =
      CF_EXPECT(RunCvdRemoteGetConfig("service_url"));
  CF_EXPECT(config.project == "google.com:android-treehugger-developer" &&
            cvdr_service_url ==
                "http://android-treehugger-developer.googleplex.com");
  std::string cvdr_zone = CF_EXPECT(RunCvdRemoteGetConfig("zone"));
  CF_EXPECT(config.zone == cvdr_zone);
  cvd::Response response;
  response.mutable_command_response();
  return response;
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
  {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    CF_EXPECT(!interrupted_, "Interrupted");
    auto subprocess = cmd.Start();
    CF_EXPECT(subprocess.Started());
    CF_EXPECT(waiter_.Setup(std::move(subprocess)));
  }
  siginfo_t siginfo = CF_EXPECT(waiter_.Wait());
  {
    // Force the destructor to run by moving it into a smaller scope.
    // This is necessary to close the write end of the pipe.
    Command forceDelete = std::move(cmd);
  }
  stdout_pipe_write->Close();
  stdout_thread.join();
  CF_EXPECT(siginfo.si_status == EXIT_SUCCESS);
  stdout_.erase(stdout_.find('\n'));
  return stdout_;
}

std::unique_ptr<CvdServerHandler> NewTryAcloudCommand(
    std::atomic<bool>& optout) {
  return std::unique_ptr<CvdServerHandler>(new TryAcloudCommand(optout));
}

}  // namespace cuttlefish
