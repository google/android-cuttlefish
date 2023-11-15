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

#include "host/commands/cvd/server_command/fetch.h"

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/result.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

class CvdFetchCommandHandler : public CvdServerHandler {
 public:
  CvdFetchCommandHandler(SubprocessWaiter& subprocess_waiter)
      : subprocess_waiter_(subprocess_waiter),
        fetch_cmd_list_{std::vector<std::string>{"fetch", "fetch_cvd"}} {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  Result<void> Interrupt() override;
  cvd_common::Args CmdList() const override { return fetch_cmd_list_; }

 private:
  SubprocessWaiter& subprocess_waiter_;
  std::mutex interruptible_;
  bool interrupted_ = false;
  std::vector<std::string> fetch_cmd_list_;
};

Result<bool> CvdFetchCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(fetch_cmd_list_, invocation.command);
}

Result<cvd::Response> CvdFetchCommandHandler::Handle(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  if (interrupted_) {
    return CF_ERR("Interrupted");
  }
  CF_EXPECT(CanHandle(request));

  Command command("/proc/self/exe");
  command.SetName("fetch_cvd");
  command.SetExecutable("/proc/self/exe");

  for (const auto& argument : ParseInvocation(request.Message()).arguments) {
    command.AddParameter(argument);
  }

  command.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, request.In());
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, request.Out());
  command.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, request.Err());
  SubprocessOptions options;

  const auto& command_request = request.Message().command_request();
  if (command_request.wait_behavior() == cvd::WAIT_BEHAVIOR_START) {
    options.ExitWithParent(false);
  }

  const auto& working_dir = command_request.working_directory();
  if (!working_dir.empty()) {
    auto fd = SharedFD::Open(working_dir, O_RDONLY | O_PATH | O_DIRECTORY);
    if (fd->IsOpen()) {
      command.SetWorkingDirectory(fd);
    }
  }

  CF_EXPECT(subprocess_waiter_.Setup(command.Start(options)));

  if (command_request.wait_behavior() == cvd::WAIT_BEHAVIOR_START) {
    cvd::Response response;
    response.mutable_command_response();
    response.mutable_status()->set_code(cvd::Status::OK);
    return response;
  }

  interrupt_lock.unlock();

  auto infop = CF_EXPECT(subprocess_waiter_.Wait());

  return ResponseFromSiginfo(infop);
}

Result<void> CvdFetchCommandHandler::Interrupt() {
  std::scoped_lock interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

std::unique_ptr<CvdServerHandler> NewCvdFetchCommandHandler(
    SubprocessWaiter& subprocess_waiter) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdFetchCommandHandler(subprocess_waiter));
}

}  // namespace cuttlefish
