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

#include <thread>

#include <android-base/file.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "host/commands/cvd/acloud/converter.h"
#include "host/commands/cvd/acloud/create_converter_parser.h"
#include "host/commands/cvd/command_sequence.h"
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

 private:
  Result<cvd::InstanceGroupInfo> ParseStartResponse(
      const cvd::Response& start_response);
  Result<void> PrintBriefSummary(const cvd::InstanceGroupInfo& group_info,
                                 std::ostream& out) const;
  Result<ConvertedAcloudCreateCommand> ValidateLocal(
      const RequestWithStdio& request);
  bool ValidateRemoteArgs(const RequestWithStdio& request);
  Result<cvd::Response> HandleLocal(const ConvertedAcloudCreateCommand& command,
                                    const RequestWithStdio& request);
  Result<void> PrepareForDeleteCommand(const cvd::InstanceGroupInfo&);
  Result<cvd::Response> HandleRemote(const RequestWithStdio& request);
  Result<void> RunAcloudConnect(const RequestWithStdio& request,
                                const std::string& hostname);

  CommandSequenceExecutor& executor_;
};

Result<cvd::InstanceGroupInfo> AcloudCommand::ParseStartResponse(
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
    const cvd::InstanceGroupInfo& group_info, std::ostream& out) const {
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
  out << std::endl << "Created instance group: " << group_name << std::endl;
  for (size_t i = 0; i != instance_ids.size(); i++) {
    std::string device_name = group_name + "-" + instance_names[i];
    out << "  " << device_name << " (local-instance-" << instance_ids[i] << ")"
        << std::endl;
  }
  out << std::endl
      << "acloud list or cvd fleet for more information." << std::endl;
  return {};
}

Result<ConvertedAcloudCreateCommand> AcloudCommand::ValidateLocal(
    const RequestWithStdio& request) {
  CF_EXPECT(CanHandle(request));
  CF_EXPECT(IsSubOperationSupported(request));
  // ConvertAcloudCreate converts acloud to cvd commands.
  return acloud_impl::ConvertAcloudCreate(request);
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

  cvd::Response response;
  response.mutable_command_response();
  auto group_info_result = ParseStartResponse(start_response);
  if (!group_info_result.ok()) {
    LOG(ERROR) << "Failed to analyze the cvd start response.";
    return response;
  }
  auto prepare_delete_result = PrepareForDeleteCommand(*group_info_result);
  if (!prepare_delete_result.ok()) {
    LOG(ERROR) << prepare_delete_result.error().FormatForEnv();
    LOG(WARNING) << "Failed to prepare for execution of `acloud delete`, use "
                    "`cvd rm` instead";
  }
  // print
  std::optional<SharedFD> fd_opt;
  if (command.verbose) {
    PrintBriefSummary(*group_info_result, request.Err());
  }
  return response;
}

// Acloud delete is not translated because it needs to handle remote cases.
// Python acloud implements delete by calling stop_cvd
// This function replaces stop_cvd with a script that calls `cvd rm`, which in
// turn calls cvd_internal_stop if necessary.
Result<void> AcloudCommand::PrepareForDeleteCommand(
    const cvd::InstanceGroupInfo& group_info) {
  std::string host_path = group_info.host_artifacts_path();
  std::string stop_cvd_path = fmt::format("{}/bin/stop_cvd", host_path);
  std::string cvd_internal_stop_path =
      fmt::format("{}/bin/cvd_internal_stop", host_path);
  if (FileExists(cvd_internal_stop_path)) {
    // cvd_internal_stop exists, stop_cvd is just a symlink to it
    CF_EXPECT(RemoveFile(stop_cvd_path), "Failed to remove stop_cvd file");
  } else {
    // cvd_internal_stop doesn't exist, stop_cvd is the actual executable file
    CF_EXPECT(RenameFile(stop_cvd_path, cvd_internal_stop_path),
              "Failed to rename stop_cvd as cvd_internal_stop");
  }
  SharedFD stop_cvd_fd = SharedFD::Creat(stop_cvd_path, 0775);
  CF_EXPECTF(stop_cvd_fd->IsOpen(), "Failed to create stop_cvd executable: {}",
             stop_cvd_fd->StrError());
  // Don't include the group name in the rm command, it's not needed for a
  // single instance group and won't know which group needs to be removed if
  // multiple groups exist. Acloud delete will set the HOME variable, which
  // means cvd rm will pick the right group.
  std::string stop_cvd_content =  "#!/bin/sh\ncvd rm";
  auto ret = WriteAll(stop_cvd_fd, stop_cvd_content);
  CF_EXPECT(ret == (ssize_t)stop_cvd_content.size(),
            "Failed to write to stop_cvd script");
  return {};
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
  if (request.IsNullIo()) {
    SharedFD null_fd = SharedFD::Open("/dev/null", O_RDWR, 0644);
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, null_fd);
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, null_fd);
  }

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
  request.Err()
      << "UPDATE! Try the new `cvdr` tool directly. Run `cvdr --help` to get "
         "started.\n";

  siginfo_t siginfo;

  cmd.Start().Wait(&siginfo, WEXITED);
  {
    // Force the destructor to run by moving it into a smaller scope.
    // This is necessary to close the write end of the pipe.
    Command forceDelete = std::move(cmd);
  }
  stdout_pipe_write->Close();
  stdout_thread.join();
  request.Out() << stdout_;
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

  if (request.IsNullIo()) {
    SharedFD null_fd = SharedFD::Open("/dev/null", O_RDWR, 0644);
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdIn, null_fd);
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, null_fd);
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, null_fd);
  }

  cmd.Start().Wait();

  return {};
}

std::unique_ptr<CvdServerHandler> NewAcloudCommand(
    CommandSequenceExecutor& executor) {
  return std::unique_ptr<CvdServerHandler>(new AcloudCommand(executor));
}

}  // namespace cuttlefish
