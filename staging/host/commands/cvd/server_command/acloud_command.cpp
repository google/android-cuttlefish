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

#include "host/commands/cvd/server_command/acloud_command.h"

#include <atomic>
#include <mutex>
#include <thread>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/acloud/converter.h"
#include "host/commands/cvd/acloud/create_converter_parser.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/instance_lock.h"
#include "host/commands/cvd/server_command/acloud_common.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    R"(Toggles translation of acloud commands to run through cvd if supported)";

constexpr char kDetailedHelpText[] = R"(
Usage:
cvd acloud translator (--opt-out|--opt-in)
Any acloud command will by default (and if supported by cvd) be translated to the appropriate cvd command and executed.
If not supported by cvd, acloud will be used.

To opt out or opt back in, run this translation toggle.
)";

}  // namespace

class AcloudCommand : public CvdServerHandler {
 public:
  AcloudCommand(CommandSequenceExecutor& executor) : executor_(executor) {}
  ~AcloudCommand() = default;

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    if (invocation.arguments.size() >= 2) {
      if (invocation.command == "acloud" &&
          (invocation.arguments[0] == "translator" ||
           invocation.arguments[0] == "mix-super-image")) {
        return false;
      }
    }
    return invocation.command == "acloud";
  }

  cvd_common::Args CmdList() const override { return {"acloud"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kDetailedHelpText;
  }

  /**
   * The `acloud` command satisfies the original `acloud CLI` command using
   * either:
   *
   * 1. `cvd` for local instance management
   *
   * 2. Or `cvdr` for remote instance management.
   *
   */
  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    auto result = ValidateLocal(request);
    if (result.ok()) {
      return CF_EXPECT(HandleLocal(*result, request));
    } else if (ValidateRemoteArgs(request)) {
      return CF_EXPECT(HandleRemote(request));
    }
    CF_EXPECT(std::move(result));
    return {};
  }

  Result<void> Interrupt() override {
    std::scoped_lock interrupt_lock(interrupt_mutex_);
    interrupted_ = true;
    CF_EXPECT(waiter_.Interrupt());
    CF_EXPECT(executor_.Interrupt());
    return {};
  }

 private:
  Result<cvd::InstanceGroupInfo> HandleStartResponse(
      const cvd::Response& start_response);
  Result<void> PrintBriefSummary(const cvd::InstanceGroupInfo& group_info,
                                 std::optional<SharedFD> stream_fd) const;
  Result<ConvertedAcloudCreateCommand> ValidateLocal(
      const RequestWithStdio& request);
  bool ValidateRemoteArgs(const RequestWithStdio& request);
  Result<cvd::Response> HandleLocal(const ConvertedAcloudCreateCommand& command,
                                    const RequestWithStdio& request);
  Result<cvd::Response> HandleRemote(const RequestWithStdio& request);
  Result<void> RunAcloudConnect(const RequestWithStdio& request,
                                const std::string& hostname);

  CommandSequenceExecutor& executor_;
  std::mutex interrupt_mutex_;
  bool interrupted_ = false;
  SubprocessWaiter waiter_;
};

Result<cvd::InstanceGroupInfo> AcloudCommand::HandleStartResponse(
    const cvd::Response& start_response) {
  CF_EXPECT(start_response.has_command_response(),
            "cvd start did not return a command response.");
  const auto& start_command_response = start_response.command_response();
  CF_EXPECT(start_command_response.has_instance_group_info(),
            "cvd start command response did not return instance_group_info.");
  cvd::InstanceGroupInfo group_info =
      start_command_response.instance_group_info();
  return group_info;
}

Result<void> AcloudCommand::PrintBriefSummary(
    const cvd::InstanceGroupInfo& group_info,
    std::optional<SharedFD> stream_fd) const {
  if (!stream_fd) {
    return {};
  }
  SharedFD fd = *stream_fd;
  std::stringstream ss;
  const std::string& group_name = group_info.group_name();
  CF_EXPECT_EQ(group_info.home_directories().size(), 1);
  const std::string home_dir = (group_info.home_directories())[0];
  std::vector<std::string> instance_names;
  std::vector<unsigned> instance_ids;
  instance_names.reserve(group_info.instances().size());
  instance_ids.reserve(group_info.instances().size());
  for (const auto& instance : group_info.instances()) {
    instance_names.push_back(instance.name());
    instance_ids.push_back(instance.instance_id());
  }
  ss << std::endl << "Created instance group: " << group_name << std::endl;
  for (size_t i = 0; i != instance_ids.size(); i++) {
    std::string device_name = group_name + "-" + instance_names[i];
    ss << "  " << device_name << " (local-instance-" << instance_ids[i] << ")"
       << std::endl;
  }
  ss << std::endl
     << "acloud list or cvd fleet for more information." << std::endl;
  auto n_write = WriteAll(*stream_fd, ss.str());
  CF_EXPECT_EQ(n_write, ss.str().size());
  return {};
}

Result<ConvertedAcloudCreateCommand> AcloudCommand::ValidateLocal(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interrupt_mutex_);
  bool lock_released = false;
  CF_EXPECT(!interrupted_, "Interrupted");
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(IsSubOperationSupported(request));
  // ConvertAcloudCreate may lock and unlock the lock
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
  auto result =
      acloud_impl::ConvertAcloudCreate(request, waiter_, cb_unlock, cb_lock);
  if (!lock_released) {
    interrupt_lock.unlock();
  }
  return result;
}

bool AcloudCommand::ValidateRemoteArgs(const RequestWithStdio& request) {
  auto args = ParseInvocation(request.Message()).arguments;
  return acloud_impl::CompileFromAcloudToCvdr(args).ok();
}

Result<cvd::Response> AcloudCommand::HandleLocal(
    const ConvertedAcloudCreateCommand& command,
    const RequestWithStdio& request) {
  CF_EXPECT(executor_.Execute(command.prep_requests, request.Err()));
  auto start_response =
      CF_EXPECT(executor_.ExecuteOne(command.start_request, request.Err()));

  if (!command.fetch_command_str.empty()) {
    // has cvd fetch command, update the fetch cvd command file
    using android::base::WriteStringToFile;
    CF_EXPECT(WriteStringToFile(command.fetch_command_str,
                                command.fetch_cvd_args_file),
              true);
  }

  auto handle_response_result = HandleStartResponse(start_response);
  if (handle_response_result.ok()) {
    // print
    std::optional<SharedFD> fd_opt;
    if (command.verbose) {
      fd_opt = request.Err();
    }
    auto write_result = PrintBriefSummary(*handle_response_result, fd_opt);
    if (!write_result.ok()) {
      LOG(ERROR) << "Failed to write the start response report.";
    }
  } else {
    LOG(ERROR) << "Failed to analyze the cvd start response.";
  }
  cvd::Response response;
  response.mutable_command_response();
  return response;
}

Result<cvd::Response> AcloudCommand::HandleRemote(
    const RequestWithStdio& request) {
  auto args = ParseInvocation(request.Message()).arguments;
  args = CF_EXPECT(acloud_impl::CompileFromAcloudToCvdr(args));
  Command cmd = Command("cvdr");
  for (auto a : args) {
    cmd.AddParameter(a);
  }
  // Do not perform ADB connection with `cvdr` until acloud CLI is fully
  // deprecated.
  if (args[0] == "create") {
    cmd.AddParameter("--auto_connect=false");
  }
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, request.In());
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, request.Err());
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
  WriteAll(request.Err(),
           "UPDATE! Try the new `cvdr` tool directly. Run `cvdr --help` to get "
           "started.\n");
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
  WriteAll(request.Out(), stdout_);
  if (args[0] == "create" && siginfo.si_status == EXIT_SUCCESS) {
    std::string hostname = stdout_.substr(0, stdout_.find(" "));
    CF_EXPECT(RunAcloudConnect(request, hostname));
  }
  cvd::Response response;
  response.mutable_command_response();
  return response;
}

Result<void> AcloudCommand::RunAcloudConnect(const RequestWithStdio& request,
                                             const std::string& hostname) {
  auto build_top = StringFromEnv("ANDROID_BUILD_TOP", "");
  CF_EXPECT(
      build_top != "",
      "Missing ANDROID_BUILD_TOP environment variable. Please run `source "
      "build/envsetup.sh`");
  Command cmd =
      Command(build_top + "/prebuilts/asuite/acloud/linux-x86/acloud");
  cmd.AddParameter("reconnect");
  cmd.AddParameter("--instance-names");
  cmd.AddParameter(hostname);
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, request.In());
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, request.Out());
  cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, request.Err());
  {
    std::unique_lock interrupt_lock(interrupt_mutex_);
    CF_EXPECT(!interrupted_, "Interrupted");
    auto subprocess = cmd.Start();
    CF_EXPECT(subprocess.Started());
    CF_EXPECT(waiter_.Setup(std::move(subprocess)));
  }
  CF_EXPECT(waiter_.Wait());
  return {};
}

std::unique_ptr<CvdServerHandler> NewAcloudCommand(
    CommandSequenceExecutor& executor) {
  return std::unique_ptr<CvdServerHandler>(new AcloudCommand(executor));
}

}  // namespace cuttlefish
