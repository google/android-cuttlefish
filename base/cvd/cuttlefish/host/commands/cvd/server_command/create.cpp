/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "host/commands/cvd/server_command/create.h"

#include <sys/types.h>

#include <cstdlib>
#include <optional>
#include <string>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/users.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"
#include "host/commands/cvd/command_sequence.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/selector/creation_analyzer.h"
#include "host/commands/cvd/selector/instance_database_types.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/server_handler.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace {

constexpr char kSummaryHelpText[] =
    "Create a Cuttlefish virtual device or environment";

constexpr char kDetailedHelpText[] =
    R"""(
Usage:
cvd create [--product_path=PATH] [--host_path=PATH] [--[no]start] [START_ARGS]
cvd create --config_file=PATH [--[no]start]

Creates and starts a new cuttlefish instance group.

--host_path=PATH     The path to the directory containing the Cuttlefish Host
                     Artifacts. Defaults to the value of $ANDROID_HOST_OUT,
                     $HOME or the current directory.

--product_path=PATH  The path(s) to the directory containing the Cuttlefish
                     Guest Images. Defaults to the value of
                     $ANDROID_PRODUCT_OUT, $HOME or the current directory.

--[no]start          Whether to start the instance group. True by default.
--config_file=PATH   Path to an environment config file to be loaded.

All other arguments are passed verbatim to cvd start, for a list of supported
arguments run `cvd start --help`.
)""";

std::string DefaultHostPath(const cvd_common::Envs& envs) {
  for (const auto& key : {kAndroidHostOut, kAndroidSoongHostOut, "HOME"}) {
    auto it = envs.find(key);
    if (it != envs.end()) {
      return it->second;
    }
  }
  return CurrentDirectory();
}

std::string DefaultProductPath(const cvd_common::Envs& envs) {
  for (const auto& key : {kAndroidProductOut, "HOME"}) {
    auto it = envs.find(key);
    if (it != envs.end()) {
      return it->second;
    }
  }
  return CurrentDirectory();
}

struct CreateFlags {
  std::string host_path;
  std::string product_path;
  bool start;
  std::string config_file;
};

Result<CreateFlags> ParseCommandFlags(const cvd_common::Envs& envs,
                                      cvd_common::Args& args) {
  CreateFlags flag_values{
      .host_path = DefaultHostPath(envs),
      .product_path = DefaultProductPath(envs),
      .start = true,
      .config_file = "",
  };
  std::vector<Flag> flags = {
      GflagsCompatFlag("host_path", flag_values.host_path),
      GflagsCompatFlag("product_path", flag_values.product_path),
      GflagsCompatFlag("start", flag_values.start),
      GflagsCompatFlag("config_file", flag_values.config_file),
  };
  CF_EXPECT(ConsumeFlags(flags, args));
  return flag_values;
}

RequestWithStdio CreateLoadCommand(const RequestWithStdio& request,
                                   cvd_common::Args& args,
                                   const std::string& config_file) {
  cvd::Request request_proto;
  auto& command = *request_proto.mutable_command_request();
  *command.mutable_env() = request.Message().command_request().env();
  command.set_working_directory(
      request.Message().command_request().working_directory());
  command.add_args("cvd");
  command.add_args("load");
  for (const auto& arg : args) {
    command.add_args(arg);
  }
  command.add_args(config_file);
  return RequestWithStdio::InheritIo(request_proto, request);
}

RequestWithStdio CreateStartCommand(const RequestWithStdio& request,
                                    const selector::LocalInstanceGroup& group,
                                    const cvd_common::Args& args,
                                    const cvd_common::Envs& envs) {
  cvd::Request request_proto;
  auto& command = *request_proto.mutable_command_request();
  for (const auto& [key, value] : envs) {
    (*command.mutable_env())[key] = value;
  }
  command.set_working_directory(
      request.Message().command_request().working_directory());
  command.mutable_selector_opts()->clear_args();
  command.mutable_selector_opts()->add_args("--group_name");
  command.mutable_selector_opts()->add_args(group.GroupName());
  command.add_args("cvd");
  command.add_args("start");
  for (const auto& arg : args) {
    command.add_args(arg);
  }
  return RequestWithStdio::InheritIo(request_proto, request);
}

Result<cvd_common::Envs> GetEnvs(const RequestWithStdio& request) {
  cvd_common::Envs envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  if (auto it = envs.find("HOME"); it != envs.end() && it->second.empty()) {
    envs.erase(it);
  }
  if (Contains(envs, "HOME")) {
    // As the end-user may override HOME, this could be a relative path
    // to client's pwd, or may include "~" which is the client's actual
    // home directory.
    auto client_pwd = request.Message().command_request().working_directory();
    const auto given_home_dir = envs.at("HOME");
    // Substituting ~ is not supported by cvd
    CF_EXPECT(!android::base::StartsWith(given_home_dir, "~") &&
                  !android::base::StartsWith(given_home_dir, "~/"),
              "The HOME directory should not start with ~");
    envs["HOME"] = CF_EXPECT(
        EmulateAbsolutePath({.current_working_dir = client_pwd,
                             .home_dir = CF_EXPECT(SystemWideUserHome()),
                             .path_to_convert = given_home_dir,
                             .follow_symlink = false}));
  }
  return envs;
}

cvd::InstanceGroupInfo GroupInfoFromGroup(
    const selector::LocalInstanceGroup& group) {
  cvd::InstanceGroupInfo info;
  info.set_group_name(group.GroupName());
  for (const auto& instance : group.Instances()) {
    cvd::InstanceGroupInfo::PerInstanceInfo instance_info;
    instance_info.set_name(instance.name());
    instance_info.set_instance_id(instance.id());
    *info.add_instances() = instance_info;
  }
  info.add_home_directories(group.HomeDir());
  info.set_host_artifacts_path(group.HostArtifactsPath());
  return info;
}

}  // namespace

class CvdCreateCommandHandler : public CvdServerHandler {
 public:
  CvdCreateCommandHandler(InstanceManager& instance_manager,
                          HostToolTargetManager& host_tool_target_manager,
                          CommandSequenceExecutor& command_executor)
      : instance_manager_(instance_manager),
        host_tool_target_manager_(host_tool_target_manager),
        command_executor_(command_executor) {}

  Result<bool> CanHandle(const RequestWithStdio& request) const override;
  Result<cvd::Response> Handle(const RequestWithStdio& request) override;
  std::vector<std::string> CmdList() const override;
  Result<std::string> SummaryHelp() const override;
  bool ShouldInterceptHelp() const override;
  Result<std::string> DetailedHelp(std::vector<std::string>&) const override;

 private:
  Result<selector::LocalInstanceGroup> GetOrCreateGroup(
      const cvd_common::Args& subcmd_args, const cvd_common::Envs& envs,
      const RequestWithStdio& request);

  static void MarkLockfiles(std::vector<InstanceLockFile>& lock_files,
                            const InUseState state);
  static void MarkLockfilesInUse(std::vector<InstanceLockFile>& lock_files) {
    MarkLockfiles(lock_files, InUseState::kInUse);
  }

  InstanceManager& instance_manager_;
  HostToolTargetManager& host_tool_target_manager_;
  CommandSequenceExecutor& command_executor_;
};

void CvdCreateCommandHandler::MarkLockfiles(
    std::vector<InstanceLockFile>& lock_files, const InUseState state) {
  for (auto& lock_file : lock_files) {
    auto result = lock_file.Status(state);
    if (!result.ok()) {
      LOG(ERROR) << result.error().FormatForEnv();
    }
  }
}

Result<bool> CvdCreateCommandHandler::CanHandle(
    const RequestWithStdio& request) const {
  auto invocation = ParseInvocation(request.Message());
  return Contains(CmdList(), invocation.command);
}

Result<selector::LocalInstanceGroup> CvdCreateCommandHandler::GetOrCreateGroup(
    const std::vector<std::string>& subcmd_args, const cvd_common::Envs& envs,
    const RequestWithStdio& request) {
  using CreationAnalyzerParam =
      selector::CreationAnalyzer::CreationAnalyzerParam;
  const auto& selector_opts =
      request.Message().command_request().selector_opts();
  const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());
  CreationAnalyzerParam analyzer_param{
      .cmd_args = subcmd_args, .envs = envs, .selector_args = selector_args};

  auto analyzer = CF_EXPECT(instance_manager_.CreationAnalyzer(analyzer_param));
  auto group_creation_info = CF_EXPECT(analyzer.ExtractGroupInfo());

  std::vector<InstanceLockFile> lock_files;
  for (auto& instance : group_creation_info.instances) {
    CF_EXPECT(instance.instance_file_lock_.has_value(),
              "Expected instance lock");
    lock_files.emplace_back(std::move(*instance.instance_file_lock_));
  }

  auto groups = CF_EXPECT(instance_manager_.FindGroups(selector::Query(
      selector::kGroupNameField, group_creation_info.group_name)));
  CF_EXPECTF(groups.size() <= 1,
             "Expected no more than one group with given name: {}",
             group_creation_info.group_name);
  // When loading an environment spec file the group is already in the database
  // in PREPARING state. Otherwise the group must be created.
  if (groups.empty()) {
    groups.push_back(
        CF_EXPECT(instance_manager_.CreateInstanceGroup(group_creation_info)));
  } else {
    auto& group = groups[0];
    CF_EXPECTF(group.Instances().size() == group_creation_info.instances.size(),
               "Mismatch in number of instances from analisys: {} vs {}",
               group.Instances().size(), group_creation_info.instances.size());
    // The instances don't have an id yet
    for (size_t i = 0; i < group.Instances().size(); ++i) {
      auto& instance = group.Instances()[i];
      auto& instance_info = group_creation_info.instances[i];
      instance.set_id(instance_info.instance_id_);
    }
    CF_EXPECT(instance_manager_.UpdateInstanceGroup(group));
  }
  // The lock must be held for as long as the group's instances are in the
  // database with the id set.
  MarkLockfilesInUse(lock_files);
  return groups[0];
}

Result<cvd::Response> CvdCreateCommandHandler::Handle(
    const RequestWithStdio& request) {
  CF_EXPECT(CanHandle(request));
  auto [subcmd, subcmd_args] = ParseInvocation(request.Message());
  bool is_help = CF_EXPECT(IsHelpSubcmd(subcmd_args));
  CF_EXPECT(!is_help);

  cvd_common::Envs envs = CF_EXPECT(GetEnvs(request));
  CreateFlags flags = CF_EXPECT(ParseCommandFlags(envs, subcmd_args));

  if (!flags.config_file.empty()) {
    auto subrequest =
        CreateLoadCommand(request, subcmd_args, flags.config_file);
    return CF_EXPECT(command_executor_.ExecuteOne(subrequest, request.Err()));
  }

  // Validate the host artifacts path before proceeding
  (void)CF_EXPECT(host_tool_target_manager_.ExecBaseName({
                      .artifacts_path = flags.host_path,
                      .op = "start",
                  }),
                  "\nMaybe try `cvd fetch` or running `lunch "
                  "<target>` to enable starting a CF device?");
  // CreationAnalyzer needs these to be set in the environment
  envs[kAndroidHostOut] = flags.host_path;
  envs[kAndroidProductOut] = flags.product_path;
  auto group = CF_EXPECT(GetOrCreateGroup(subcmd_args, envs, request));

  group.SetAllStates(cvd::INSTANCE_STATE_STOPPED);
  group.SetStartTime(selector::CvdServerClock::now());
  instance_manager_.UpdateInstanceGroup(group);

  cvd::Response response;
  response.mutable_status()->set_code(cvd::Status::OK);

  if (flags.start) {
    auto start_cmd = CreateStartCommand(request, group, subcmd_args, envs);
    response =
        CF_EXPECT(command_executor_.ExecuteOne(start_cmd, request.Err()));
  }

  *response.mutable_command_response()->mutable_instance_group_info() =
      GroupInfoFromGroup(group);
  return response;
}

std::vector<std::string> CvdCreateCommandHandler::CmdList() const {
  return {"create"};
}

Result<std::string> CvdCreateCommandHandler::SummaryHelp() const {
  return kSummaryHelpText;
}

bool CvdCreateCommandHandler::ShouldInterceptHelp() const { return true; }

Result<std::string> CvdCreateCommandHandler::DetailedHelp(
    std::vector<std::string>&) const {
  return kDetailedHelpText;
}

std::unique_ptr<CvdServerHandler> NewCvdCreateCommandHandler(
    InstanceManager& instance_manager,
    HostToolTargetManager& host_tool_target_manager,
    CommandSequenceExecutor& executor) {
  return std::unique_ptr<CvdServerHandler>(new CvdCreateCommandHandler(
      instance_manager, host_tool_target_manager, executor));
}

}  // namespace cuttlefish

