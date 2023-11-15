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

#include "host/commands/cvd/server_command/restart.h"

#include <sys/types.h>

#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <android-base/file.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/epoll_loop.h"
#include "host/commands/cvd/frontline_parser.h"
#include "host/commands/cvd/instance_manager.h"
#include "host/commands/cvd/server.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"
#include "host/libs/web/build_api.h"
#include "host/libs/web/build_string.h"

namespace cuttlefish {
namespace {

constexpr char kRestartServerHelpMessage[] =
    R"(Cuttlefish Virtual Device (CVD) CLI.

usage: cvd restart-server <common args> <mode> <mode args>

Common Args:
  --help                 Print out this message
  --verbose              Control verbose mode

Modes:
  match-client           Use the client executable.
  latest                 Download the latest executable
  reuse-server           Use the server executable.
)";

Result<SharedFD> LatestCvdAsFd(BuildApi& build_api) {
  static constexpr char kTarget[] =
      "aosp_cf_x86_64_phone-trunk_staging-userdebug";
  const auto build_string =
      DeviceBuildString{.branch_or_id = "aosp-main", .target = kTarget};
  Build build = CF_EXPECT(build_api.GetBuild(build_string, kTarget));
  CF_EXPECT(std::holds_alternative<DeviceBuild>(build),
            "Unable to process non-DeviceBuild. Something has gone wrong.");
  DeviceBuild device_build = *std::get_if<DeviceBuild>(&build);

  auto fd = SharedFD::MemfdCreate("cvd");
  CF_EXPECT(fd->IsOpen(), "MemfdCreate failed: " << fd->StrError());

  auto write = [fd](char* data, size_t size) -> bool {
    if (size == 0) {
      return true;
    }
    auto written = WriteAll(fd, data, size);
    if (written != size) {
      LOG(ERROR) << "Failed to persist data: " << fd->StrError();
      return false;
    }
    return true;
  };
  CF_EXPECT(build_api.ArtifactToCallback(device_build, "cvd", write));

  return fd;
}

class CvdRestartHandler : public CvdServerHandler {
 public:
  CvdRestartHandler(BuildApi& build_api, CvdServer& server,
                    InstanceManager& instance_manager)
      : build_api_(build_api),
        supported_modes_({"match-client", "latest", "reuse-server"}),
        server_(server),
        instance_manager_(instance_manager) {
    flags_.EnrollFlag(CvdFlag<bool>("help", false));
    flags_.EnrollFlag(CvdFlag<bool>("verbose", false));
    // If the fla is false, the request will fail if there's on-going requests
    // If true, calls Stop()
    flags_.EnrollFlag(CvdFlag<bool>("force", true));
  }

  Result<bool> CanHandle(const RequestWithStdio& request) const override {
    auto invocation = ParseInvocation(request.Message());
    return android::base::Basename(invocation.command) == kRestartServer;
  }

  Result<cvd::Response> Handle(const RequestWithStdio& request) override {
    /*
     * TODO(weihsu@): change the code accordingly per verbosity level control.
     *
     * Now, the server can start with a verbosity level. Change the code
     * accordingly.
     */
    CF_EXPECT(CanHandle(request));
    cvd::Response response;
    if (request.Message().has_shutdown_request()) {
      response.mutable_shutdown_response();
    } else {
      CF_EXPECT(
          request.Message().has_command_request(),
          "cvd restart request must be either command or shutdown request.");
      response.mutable_command_response();
    }
    // all_args[0] = "cvd", all_args[1] = "restart_server"
    cvd_common::Args all_args =
        cvd_common::ConvertToArgs(request.Message().command_request().args());
    CF_EXPECT_GE(all_args.size(), 2);
    CF_EXPECT_EQ(all_args.at(0), "cvd");
    CF_EXPECT_EQ(all_args.at(1), kRestartServer);
    // erase the first item, "cvd"
    all_args.erase(all_args.begin());

    auto parsed = CF_EXPECT(Parse(all_args));
    if (parsed.help) {
      const std::string help_message(kRestartServerHelpMessage);
      WriteAll(request.Out(), help_message);
      return response;
    }

    // On CF_ERR, the locks will be released automatically
    WriteAll(request.Out(), "Stopping the cvd_server.\n");
    server_.Stop();

    CF_EXPECT(request.Credentials() != std::nullopt);
    const uid_t client_uid = request.Credentials()->uid;
    auto json_string =
        CF_EXPECT(SerializedInstanceDatabaseToString(client_uid));
    std::optional<SharedFD> mem_fd;
    if (instance_manager_.HasInstanceGroups(client_uid)) {
      mem_fd = CF_EXPECT(CreateMemFileWithSerializedDb(json_string));
      CF_EXPECT(mem_fd != std::nullopt && (*mem_fd)->IsOpen(),
                "mem file not open?");
    }

    if (parsed.verbose && mem_fd) {
      PrintFileLink(request.Err(), *mem_fd);
    }

    const std::string subcmd = parsed.subcmd.value_or("reuse-server");
    SharedFD new_exe;
    CF_EXPECT(Contains(supported_modes_, subcmd),
              "unsupported subcommand :" << subcmd);
    if (subcmd == "match-client") {
      CF_EXPECT(request.Extra(), "match-client requires the file descriptor.");
      new_exe = *request.Extra();
    } else if (subcmd == "latest") {
      new_exe = CF_EXPECT(LatestCvdAsFd(build_api_));
    } else if (subcmd == "reuse-server") {
      new_exe = CF_EXPECT(NewExecFromPath(request, kServerExecPath));
    } else {
      return CF_ERR("unsupported subcommand");
    }

    CF_EXPECT(server_.Exec({.new_exe = new_exe,
                            .carryover_client_fd = request.Client(),
                            .in_memory_data_fd = mem_fd,
                            .verbose = parsed.verbose}));
    return CF_ERR("Should be unreachable");
  }

  Result<void> Interrupt() override { return CF_ERR("Can't interrupt"); }
  cvd_common::Args CmdList() const override { return {kRestartServer}; }
  constexpr static char kRestartServer[] = "restart-server";

 private:
  struct Parsed {
    bool help;
    bool verbose;
    std::optional<std::string> subcmd;
    std::optional<std::string> exec_path;
  };
  Result<Parsed> Parse(const cvd_common::Args& args) {
    // it's ugly but let's reuse the frontline parser
    auto parser_with_result =
        CF_EXPECT(FrontlineParser::Parse({.internal_cmds = supported_modes_,
                                          .all_args = args,
                                          .cvd_flags = flags_}));
    CF_EXPECT(parser_with_result != nullptr,
              "FrontlineParser::Parse() returned nullptr");
    // If there was a subcmd, the flags for the subcmd is in SubCmdArgs().
    // If not, the flags for restart-server would be in CvdArgs()
    std::optional<std::string> subcmd_opt = parser_with_result->SubCmd();
    cvd_common::Args subcmd_args =
        (subcmd_opt ? parser_with_result->SubCmdArgs()
                    : parser_with_result->CvdArgs());
    auto name_flag_map = CF_EXPECT(flags_.CalculateFlags(subcmd_args));
    CF_EXPECT(Contains(name_flag_map, "help"));
    CF_EXPECT(Contains(name_flag_map, "verbose"));

    bool help =
        CF_EXPECT(FlagCollection::GetValue<bool>(name_flag_map.at("help")));
    bool verbose =
        CF_EXPECT(FlagCollection::GetValue<bool>(name_flag_map.at("verbose")));
    std::optional<std::string> exec_path;
    if (Contains(name_flag_map, "exec-path")) {
      exec_path = CF_EXPECT(
          FlagCollection::GetValue<std::string>(name_flag_map.at("exec-path")));
    }
    return Parsed{.help = help,
                  .verbose = verbose,
                  .subcmd = subcmd_opt,
                  .exec_path = exec_path};
  }

  Result<SharedFD> NewExecFromPath(const RequestWithStdio& request,
                                   const std::string& exec_path) {
    std::string emulated_absolute_path;
    const std::string client_pwd =
        request.Message().command_request().working_directory();
    // ~ that means $HOME is not supported
    CF_EXPECT(!android::base::StartsWith(exec_path, "~/"),
              "Path starting with ~/ is not supported.");
    CF_EXPECT_NE(
        exec_path, "~",
        "~ is not supported as a executable path, and likely is not a file.");
    emulated_absolute_path =
        CF_EXPECT(EmulateAbsolutePath({.current_working_dir = client_pwd,
                                       .path_to_convert = exec_path,
                                       .follow_symlink = false}),
                  "Failed to change exec_path to an absolute path.");
    auto new_exe = SharedFD::Open(emulated_absolute_path, O_RDONLY);
    CF_EXPECT(new_exe->IsOpen(), "Failed to open \""
                                     << exec_path << " that is "
                                     << emulated_absolute_path
                                     << "\": " << new_exe->StrError());
    return new_exe;
  }

  Result<std::string> SerializedInstanceDatabaseToString(
      const uid_t client_uid) {
    auto db_json = CF_EXPECT(instance_manager_.Serialize(client_uid),
                             "Failed to serialized instance database");
    return db_json.toStyledString();
  }

  Result<SharedFD> CreateMemFileWithSerializedDb(
      const std::string& json_string) {
    const std::string mem_file_name = "cvd_server_" + std::to_string(getpid());
    auto mem_fd = SharedFD::MemfdCreateWithData(mem_file_name, json_string);
    CF_EXPECT(mem_fd->IsOpen(),
              "MemfdCreateWithData failed: " << mem_fd->StrError());
    return mem_fd;
  }

  void PrintFileLink(const SharedFD& fd_stream, const SharedFD& mem_fd) const {
    auto link_target_result = mem_fd->ProcFdLinkTarget();
    if (!link_target_result.ok()) {
      WriteAll(fd_stream,
               "Failed to resolve the link target for the memory file.\n");
      return;
    }
    std::string message("The link target for the memory file is ");
    message.append(*link_target_result).append("\n");
    WriteAll(fd_stream, message);
    return;
  }

  BuildApi& build_api_;
  std::vector<std::string> supported_modes_;
  FlagCollection flags_;
  CvdServer& server_;
  InstanceManager& instance_manager_;
};

}  // namespace

std::unique_ptr<CvdServerHandler> NewCvdRestartHandler(
    BuildApi& build_api, CvdServer& server, InstanceManager& instance_manager) {
  return std::unique_ptr<CvdServerHandler>(
      new CvdRestartHandler(build_api, server, instance_manager));
}

}  // namespace cuttlefish
