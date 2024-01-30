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

#include "host/commands/cvd/server_command/status_fetcher.h"

#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include <fmt/core.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/selector/selector_constants.h"
#include "host/commands/cvd/server_command/utils.h"
#include "host/libs/config/config_constants.h"

namespace cuttlefish {
namespace {

struct IdAndPerInstanceName {
  IdAndPerInstanceName(std::string name, const unsigned id)
      : per_instance_name(name), id(id) {}
  IdAndPerInstanceName() = default;
  std::string per_instance_name;
  unsigned id;
};

}  // namespace

Result<void> StatusFetcher::Interrupt() {
  std::lock_guard interrupt_lock(interruptible_);
  interrupted_ = true;
  CF_EXPECT(subprocess_waiter_.Interrupt());
  return {};
}

static Result<SharedFD> CreateFileToRedirect(
    const std::string& stderr_or_stdout) {
  auto thread_id = std::this_thread::get_id();
  std::stringstream ss;
  ss << "cvd.status." << stderr_or_stdout << "." << thread_id;
  auto mem_fd_name = ss.str();
  SharedFD fd = SharedFD::MemfdCreate(mem_fd_name);
  CF_EXPECT(fd->IsOpen());
  return fd;
}

Result<StatusFetcherOutput> StatusFetcher::FetchOneInstanceStatus(
    const RequestWithStdio& request,
    const InstanceManager::LocalInstanceGroup& instance_group,
    const std::string& per_instance_name, const unsigned id) {
  std::unique_lock interrupt_lock(interruptible_);
  CF_EXPECT(!interrupted_, "Interrupted");
  auto [subcmd, cmd_args] = ParseInvocation(request.Message());

  // remove --all_instances if there is
  bool all_instances = false;
  CF_EXPECT(
      ParseFlags({GflagsCompatFlag("all_instances", all_instances)}, cmd_args));

  const auto working_dir =
      request.Message().command_request().working_directory();

  auto android_host_out = instance_group.HostArtifactsPath();
  auto home = instance_group.HomeDir();
  auto bin = CF_EXPECT(GetBin(android_host_out));
  auto bin_path = fmt::format("{}/bin/{}", android_host_out, bin);

  cvd_common::Envs envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  envs["HOME"] = home;
  // old cvd_internal_status expects CUTTLEFISH_INSTANCE=<k>
  envs[kCuttlefishInstanceEnvVarName] = std::to_string(id);

  SharedFD redirect_stdout_fd = CF_EXPECT(CreateFileToRedirect("stdout"));
  SharedFD redirect_stderr_fd = CF_EXPECT(CreateFileToRedirect("stderr"));
  ConstructCommandParam construct_cmd_param{.bin_path = bin_path,
                                            .home = home,
                                            .args = cmd_args,
                                            .envs = envs,
                                            .working_dir = working_dir,
                                            .command_name = bin,
                                            .in = request.In(),
                                            .out = redirect_stdout_fd,
                                            .err = redirect_stderr_fd};
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  CF_EXPECT(subprocess_waiter_.Setup(command.Start()));

  interrupt_lock.unlock();
  auto infop = CF_EXPECT(subprocess_waiter_.Wait());

  CF_EXPECT_EQ(redirect_stdout_fd->LSeek(0, SEEK_SET), 0);
  CF_EXPECT_EQ(redirect_stderr_fd->LSeek(0, SEEK_SET), 0);

  std::string serialized_json;
  CF_EXPECT_GE(ReadAll(redirect_stdout_fd, &serialized_json), 0);

  // old branches will print nothing
  if (serialized_json.empty()) {
    serialized_json = "[{\"warning\" : \"cvd-status-unsupported device\"}]";
  }

  std::string status_stderr;
  CF_EXPECT_GE(ReadAll(redirect_stderr_fd, &status_stderr), 0);

  auto instance_status_json = CF_EXPECT(ParseJson(serialized_json));
  CF_EXPECT_EQ(instance_status_json.size(), 1);
  instance_status_json = instance_status_json[0];
  static constexpr auto kWebrtcProp = "webrtc_device_id";
  static constexpr auto kNameProp = "instance_name";

  // Check for isObject first, calling isMember on anything else causes a
  // runtime error
  if (instance_status_json.isObject() &&
      !instance_status_json.isMember(kWebrtcProp) &&
      instance_status_json.isMember(kNameProp)) {
    // b/296644913 some cuttlefish versions printed the webrtc device id as
    // the instance name.
    instance_status_json[kWebrtcProp] = instance_status_json[kNameProp];
  }
  instance_status_json[kNameProp] = per_instance_name;

  return StatusFetcherOutput{
      .stderr_buf = status_stderr,
      .json_from_stdout = instance_status_json,
      .response = ResponseFromSiginfo(infop),
  };
}

Result<StatusFetcherOutput> StatusFetcher::FetchStatus(
    const RequestWithStdio& request) {
  std::unique_lock interrupt_lock(interruptible_);
  CF_EXPECT(!interrupted_, "Interrupted");
  const uid_t uid = CF_EXPECT(request.Credentials()).uid;
  cvd_common::Envs envs =
      cvd_common::ConvertToEnvs(request.Message().command_request().env());
  auto [subcmd, cmd_args] = ParseInvocation(request.Message());

  // find group
  const auto& selector_opts =
      request.Message().command_request().selector_opts();
  const auto selector_args = cvd_common::ConvertToArgs(selector_opts.args());
  CF_EXPECT(Contains(envs, kAndroidHostOut) &&
            DirectoryExists(envs.at(kAndroidHostOut)));

  CvdFlag<bool> all_instances_flag("all_instances");
  auto all_instances_opt = CF_EXPECT(all_instances_flag.FilterFlag(cmd_args));

  auto instance_group =
      CF_EXPECT(instance_manager_.SelectGroup(selector_args, envs, uid));

  std::vector<IdAndPerInstanceName> instance_infos;
  auto instance_record_result =
      instance_manager_.SelectInstance(selector_args, envs, uid);

  bool status_the_group_flag = all_instances_opt && *all_instances_opt;
  if (instance_record_result.ok() && !status_the_group_flag) {
    instance_infos.emplace_back(
        instance_record_result->PerInstanceName(),
        static_cast<unsigned>(instance_record_result->InstanceId()));
  } else {
    auto instances = CF_EXPECT(instance_manager_.FindInstances(
        uid, {selector::kGroupNameField, instance_group.GroupName()}));
    if (status_the_group_flag) {
      instance_infos.reserve(instances.size());
      for (const auto& instance : instances) {
        instance_infos.emplace_back(
            instance.PerInstanceName(),
            static_cast<unsigned>(instance.InstanceId()));
      }
    } else {
      std::map<int, std::string> sorted_id_name_map;
      for (const auto& instance : instances) {
        sorted_id_name_map[instance.InstanceId()] = instance.PerInstanceName();
      }
      auto first_itr = sorted_id_name_map.begin();
      instance_infos.emplace_back(first_itr->second, first_itr->first);
    }
  }
  interrupt_lock.unlock();

  std::string entire_stderr_msg;
  Json::Value instances_json(Json::arrayValue);
  for (const auto& instance_info : instance_infos) {
    auto [status_stderr, instance_status_json, instance_response] =
        CF_EXPECT(FetchOneInstanceStatus(request, instance_group,
                                         instance_info.per_instance_name,
                                         instance_info.id));
    CF_EXPECTF(instance_response.status().code() == cvd::Status::OK,
               "cvd status for {}-{} failed", instance_group.GroupName(),
               instance_info.per_instance_name);
    instances_json.append(instance_status_json);
    entire_stderr_msg.append(status_stderr);
  }

  cvd::Response response;
  response.mutable_command_response();
  response.mutable_status()->set_code(cvd::Status::OK);
  return StatusFetcherOutput{
      .stderr_buf = entire_stderr_msg,
      .json_from_stdout = instances_json,
      .response = response,
  };
}

Result<std::string> StatusFetcher::GetBin(
    const std::string& host_artifacts_path) const {
  return CF_EXPECT(host_tool_target_manager_.ExecBaseName({
      .artifacts_path = host_artifacts_path,
      .op = "status",
  }));
}

}  // namespace cuttlefish
