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

#include "host/commands/cvd/common_utils.h"

#include <memory>
#include <sstream>

#include <android-base/file.h>
#include <android-base/logging.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/files.h"

namespace cuttlefish {

cvd::Request MakeRequest(const MakeRequestParam& args_and_envs,
                         cvd::WaitBehavior wait_behavior) {
  const auto& args = args_and_envs.cmd_args;
  const auto& env = args_and_envs.env;
  const auto& selector_args = args_and_envs.selector_args;
  cvd::Request request;
  auto command_request = request.mutable_command_request();
  for (const std::string& arg : args) {
    command_request->add_args(arg);
  }
  auto selector_opts = command_request->mutable_selector_opts();
  for (const std::string& selector_arg : selector_args) {
    selector_opts->add_args(selector_arg);
  }

  for (const auto& [key, value] : env) {
    (*command_request->mutable_env())[key] = value;
  }

  if (!Contains(command_request->env(), "ANDROID_HOST_OUT")) {
    // see b/254418863
    (*command_request->mutable_env())["ANDROID_HOST_OUT"] =
        android::base::Dirname(android::base::GetExecutableDirectory());
  }

  std::unique_ptr<char, void (*)(void*)> cwd(getcwd(nullptr, 0), &free);
  command_request->set_working_directory(cwd.get());
  command_request->set_wait_behavior(wait_behavior);

  return request;
}

Result<std::string> StopBin(const std::string& android_host_out) {
  std::string bin_dir_path = android_host_out + "/bin/";
  std::vector<std::string> bins_to_try{"cvd_internal_stop", "stop_cvd"};
  std::string stop_bin;
  for (const auto& bin : bins_to_try) {
    std::stringstream bin_path;
    bin_path << bin_dir_path << bin;
    if (FileExists(bin_path.str()) && !DirectoryExists(bin_path.str())) {
      stop_bin = bin;
      break;
    }
  }
  CF_EXPECT(!stop_bin.empty(),
            "Executable to stop cvd doesn't exist in " << android_host_out);
  return stop_bin;
}

}  // namespace cuttlefish
