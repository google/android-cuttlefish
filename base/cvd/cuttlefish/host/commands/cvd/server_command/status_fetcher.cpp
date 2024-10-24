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
#include <string>
#include <vector>

#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <fmt/core.h>
#include <json/json.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "cuttlefish/host/commands/cvd/selector/cvd_persistent_data.pb.h"
#include "host/commands/cvd/selector/instance_group_record.h"
#include "host/commands/cvd/server_command/host_tool_target.h"
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
    bool first = (i == 0 || name[i - 1] == ' ');
    if (!first) {
      name[i] = std::tolower(static_cast<unsigned char>(name[i]));
    }
  }

  return name;
}

// Adds more information to the json object returned by cvd_internal_status,
// including some that cvd_internal_status normally returns but doesn't when the
// instance is not running.
void OverrideInstanceJson(const selector::LocalInstance& instance,
                          Json::Value& instance_json) {
  instance_json["instance_name"] = instance.name();
  instance_json["status"] = HumanFriendlyStateName(instance.state());
  instance_json["assembly_dir"] = instance.assembly_dir();
  instance_json["instance_dir"] = instance.instance_dir();
  instance_json["instance_name"] = instance.name();
  if (instance.IsActive()) {
    // Only running instances have id > 0, these values only make sense for
    // running instances.
    instance_json["web_access"] =
        fmt::format("https://localhost:1443/devices/{}/files/client.html",
                    instance.webrtc_device_id());
    instance_json["webrtc_device_id"] = instance.webrtc_device_id();
    instance_json["adb_port"] = instance.adb_port();
  }
}

Result<std::string> GetBin(const std::string& host_artifacts_path) {
  return CF_EXPECT(HostToolTarget(host_artifacts_path).GetStatusBinName());
}

}  // namespace

Result<Json::Value> FetchInstanceStatus(selector::LocalInstance& instance,
                                        std::chrono::seconds timeout) {
  // Only running instances are capable of responding to status requests. An
  // unreachable instance is also considered running, it just didnt't reply last
  // time.
  if (instance.state() != cvd::INSTANCE_STATE_RUNNING &&
      instance.state() != cvd::INSTANCE_STATE_UNREACHABLE) {
    Json::Value instance_json;
    instance_json["instance_name"] = instance.name();
    instance_json["status"] = HumanFriendlyStateName(instance.state());
    OverrideInstanceJson(instance, instance_json);
    return instance_json;
  }

  const auto working_dir = CurrentDirectory();

  auto android_host_out = instance.host_artifacts_path();
  auto home = instance.home_directory();
  auto bin = CF_EXPECT(GetBin(android_host_out));
  auto bin_path = fmt::format("{}/bin/{}", android_host_out, bin);

  cvd_common::Envs envs;
  envs["HOME"] = home;
  // old cvd_internal_status expects CUTTLEFISH_INSTANCE=<k>
  envs[kCuttlefishInstanceEnvVarName] = std::to_string(instance.id());
  std::vector<std::string> args{"--print", "--wait_for_launcher",
                                std::to_string(timeout.count())};

  ConstructCommandParam construct_cmd_param{
      .bin_path = bin_path,
      .home = home,
      .args = args,
      .envs = envs,
      .working_dir = working_dir,
      .command_name = bin,
  };
  Command command = CF_EXPECT(ConstructCommand(construct_cmd_param));

  std::string serialized_json;

  int res = RunWithManagedStdio(std::move(command), nullptr, &serialized_json,
                                nullptr /*stderr*/);

  // old branches will print nothing
  if (serialized_json.empty() && res == 0) {
    serialized_json = "[{\"warning\" : \"cvd-status-unsupported device\"}]";
  }

  // Parse only if the command produced output, otherwise just produce data from
  // the instance database.
  Json::Value instance_status_json(Json::objectValue);
  if (!serialized_json.empty()) {
    Json::Value json_array = CF_EXPECT(ParseJson(serialized_json),
                                       "Status tool returned invalid JSON");
    CF_EXPECTF(json_array.isArray(),
               "Status tool returned unexpected output (not an array): {}",
               serialized_json);
    CF_EXPECT_EQ(json_array.size(), 1ul);
    instance_status_json = json_array[0];
  }

  if (res != 0) {
    LOG(ERROR) << "Status tool exited with code " << res;
    instance.set_state(cvd::INSTANCE_STATE_UNREACHABLE);
    instance_status_json["warning"] = "cvd status failed";
  }
  OverrideInstanceJson(instance, instance_status_json);

  return instance_status_json;
}

}  // namespace cuttlefish
