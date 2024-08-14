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

#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fmt/core.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/cvd_server.pb.h"
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"
#include "host/commands/cvd/common_utils.h"
#include "host/commands/cvd/flag.h"
#include "host/commands/cvd/selector/instance_group_record.h"
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

// The most important thing this function does is turn "INSTANCE_STATE_RUNNING"
// into "Running". Some external tools (like the host orchestrator) already
// depend on this string.
std::string HumanFriendlyStateName(cvd::InstanceState state) {
  std::string name = cvd::InstanceState_Name(state);
  // Drop the enum name prefix
  std::string_view prefix = "INSTANCE_STATE_";
  if (android::base::StartsWith(name, prefix)) {
    name = name.substr(prefix.size());
  }

  for (size_t i = 0; i < name.size(); ++i) {
    // Replace underscores with spaces
    if (name[i] == '_') {
      name[i] = ' ';
      continue;
    }
    // All characters but the first of each word should be lowercase
    bool first = (i == 0 || name[i-1] == ' ');
    if (!first) {
      name[i] = std::tolower(static_cast<unsigned char>(name[i]));
    }
  }

  return name;
}

// Adds more information to the json object returned by cvd_internal_status,
// including some that cvd_internal_status normally returns but doesn't when the
// instance is not running.
void OverrideInstanceJson(const selector::LocalInstanceGroup& group,
                        const cvd::Instance& instance,
                        Json::Value& instance_json) {
  instance_json["instance_name"] = instance.name();
  instance_json["status"] = HumanFriendlyStateName(instance.state());
  instance_json["assembly_dir"] = group.AssemblyDir();
  instance_json["instance_dir"] = group.InstanceDir(instance);
  instance_json["instance_name"] = instance.name();
  if (selector::LocalInstanceGroup::InstanceIsActive(instance)) {
    // Only running instances have id > 0, these values only make sense for
    // running instances.
    instance_json["web_access"] =
        fmt::format("https://localhost:1443/devices/{}/files/client.html",
                    instance.webrtc_device_id());
    instance_json["webrtc_device_id"] = instance.webrtc_device_id();
    instance_json["adb_port"] = instance.adb_port();
  }
}

Result<void> UpdateInstanceWithStatusResult(
    cvd::Instance& instance, const Json::Value& instance_status_json) {
  // TODO(jemoreira): Make cvd choose the values for these and pass them to
  // cvd_internal_start so that it doesn't need to parse it from the status
  // command output.
  if (instance_status_json.isMember("adb_serial") &&
      instance_status_json["adb_serial"].isString()) {
    std::string adb_serial = instance_status_json["adb_serial"].asString();
    auto port_str = *android::base::Split(adb_serial, ":").rbegin();
    int port = 0;
    if (!android::base::ParseInt(port_str, &port)) {
      LOG(ERROR) << "Failed to parse adb port from adb serial";
    }
    instance.set_adb_port(port);
  }
  instance.set_state(cvd::INSTANCE_STATE_RUNNING);
  return {};
}

}  // namespace

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
    const InstanceManager::LocalInstanceGroup& group, cvd::Instance& instance) {
  // Only running instances are capable of responding to status requests. An
  // unreachable instance is also considered running, it just didnt't reply last
  // time.
  if (instance.state() != cvd::INSTANCE_STATE_RUNNING &&
      instance.state() != cvd::INSTANCE_STATE_UNREACHABLE) {
    Json::Value instance_json;
    instance_json["instance_name"] = instance.name();
    instance_json["status"] = HumanFriendlyStateName(instance.state());
    OverrideInstanceJson(group, instance, instance_json);
    cvd::Response response;
    response.mutable_command_response();  // set oneof field
    response.mutable_status()->set_code(cvd::Status::OK);
    return StatusFetcherOutput{
        .stderr_buf = "",
        .json_from_stdout = instance_json,
        .response = response,
    };
  }

  auto [subcmd, cmd_args] = ParseInvocation(request.Message());

  // remove --all_instances if there is
  bool all_instances = false;
  CF_EXPECT(ConsumeFlags({GflagsCompatFlag("all_instances", all_instances)},
                         cmd_args));

  const auto working_dir =
      request.Message().command_request().working_directory();

  auto android_host_out = group.Proto().host_artifacts_path();
  auto home = group.Proto().home_directory();
  auto bin = CF_EXPECT(GetBin(android_host_out));
  auto bin_path = fmt::format("{}/bin/{}", android_host_out, bin);

  cvd_common::Envs envs = request.Envs();
  envs["HOME"] = home;
  // old cvd_internal_status expects CUTTLEFISH_INSTANCE=<k>
  envs[kCuttlefishInstanceEnvVarName] = std::to_string(instance.id());

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

  siginfo_t infop;
  command.Start().Wait(&infop, WEXITED);

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
  CF_EXPECT_EQ(instance_status_json.size(), 1ul);
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
  instance_status_json[kNameProp] = instance.name();

  auto response = ResponseFromSiginfo(infop);
  if (response.status().code() == cvd::Status::OK) {
    CF_EXPECT(UpdateInstanceWithStatusResult(instance, instance_status_json));
  } else {
    instance.set_state(cvd::INSTANCE_STATE_UNREACHABLE);
    instance_status_json["warning"] = "cvd status failed";
  }
  instance_manager_.UpdateInstance(group, instance);
  OverrideInstanceJson(group, instance, instance_status_json);

  return StatusFetcherOutput{
      .stderr_buf = status_stderr,
      .json_from_stdout = instance_status_json,
      .response = response,
  };
}

Result<StatusFetcherOutput> StatusFetcher::FetchStatus(
    const RequestWithStdio& request) {
  cvd_common::Envs envs = request.Envs();
  auto [subcmd, cmd_args] = ParseInvocation(request.Message());

  // find group
  const auto selector_args = request.SelectorArgs();
  CF_EXPECT(Contains(envs, kAndroidHostOut) &&
            DirectoryExists(envs.at(kAndroidHostOut)));

  CvdFlag<bool> all_instances_flag("all_instances");
  auto all_instances_opt = CF_EXPECT(all_instances_flag.FilterFlag(cmd_args));

  auto instance_group =
      CF_EXPECT(instance_manager_.SelectGroup(selector_args, envs));

  std::vector<cvd::Instance> instances;
  auto instance_record_result =
      instance_manager_.SelectInstance(selector_args, envs);

  bool status_the_group_flag = all_instances_opt && *all_instances_opt;
  if (instance_record_result.ok() && !status_the_group_flag) {
    instances.emplace_back(instance_record_result->first);
  } else {
    if (status_the_group_flag) {
      instances = instance_group.Instances();
    } else {
      std::map<int, const cvd::Instance&> sorted_id_instance_map;
      for (const auto& instance : instance_group.Instances()) {
        sorted_id_instance_map.emplace(instance.id(), instance);
      }
      auto first_itr = sorted_id_instance_map.begin();
      instances.emplace_back(first_itr->second);
    }
  }

  std::string entire_stderr_msg;
  Json::Value instances_json(Json::arrayValue);
  for (auto& instance : instances) {
    auto [status_stderr, instance_status_json, response] =
        CF_EXPECT(FetchOneInstanceStatus(request, instance_group, instance));
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

Result<Json::Value> StatusFetcher::FetchGroupStatus(
    const RequestWithStdio& original_request,
    selector::LocalInstanceGroup& group) {
  Json::Value group_json(Json::objectValue);
  group_json["group_name"] = group.GroupName();
  group_json["start_time"] = selector::Format(group.StartTime());

  auto request_message = MakeRequest(
      {.cmd_args = {"cvd", "status", "--print", "--all_instances"},
       .env = original_request.Envs(),
       .selector_args = {"--group_name", group.GroupName()},
       .working_dir =
           original_request.Message().command_request().working_directory()});
  RequestWithStdio group_request{request_message,
                                 original_request.FileDescriptors()};
  auto [_, instances_json, group_response] =
      CF_EXPECT(FetchStatus(group_request));
  group_json["instances"] = instances_json;
  return group_json;
}

Result<std::string> StatusFetcher::GetBin(
    const std::string& host_artifacts_path) const {
  return CF_EXPECT(host_tool_target_manager_.ExecBaseName({
      .artifacts_path = host_artifacts_path,
      .op = "status",
  }));
}

}  // namespace cuttlefish
