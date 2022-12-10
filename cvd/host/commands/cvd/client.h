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

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <android-base/logging.h>
#include <android-base/result.h>
#include <build/version.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/unix_sockets.h"
#include "cvd_server.pb.h"

namespace cuttlefish {

struct OverrideFd {
  std::optional<SharedFD> stdin_override_fd;
  std::optional<SharedFD> stdout_override_fd;
  std::optional<SharedFD> stderr_override_fd;
};

class CvdClient {
 public:
  Result<void> ValidateServerVersion(const std::string& host_tool_directory,
                                     int num_retries = 1);
  Result<void> StopCvdServer(bool clear);
  Result<void> HandleAcloud(
      const std::vector<std::string>& args,
      const std::unordered_map<std::string, std::string>& env,
      const std::string& host_tool_directory);
  Result<void> HandleCommand(
      const std::vector<std::string>& args,
      const std::unordered_map<std::string, std::string>& env,
      const std::vector<std::string>& selector_args,
      const OverrideFd& control_fds);
  Result<void> HandleCommand(
      const std::vector<std::string>& args,
      const std::unordered_map<std::string, std::string>& env,
      const std::vector<std::string>& selector_args) {
    CF_EXPECT(
        HandleCommand(args, env, selector_args,
                      OverrideFd{std::nullopt, std::nullopt, std::nullopt}));
    return {};
  }
  Result<std::string> HandleVersion(const std::string& host_tool_directory);

 private:
  std::optional<UnixMessageSocket> server_;

  Result<void> SetServer(const SharedFD& server);
  Result<cvd::Response> SendRequest(const cvd::Request& request,
                                    const OverrideFd& new_control_fds = {},
                                    std::optional<SharedFD> extra_fd = {});
  Result<void> StartCvdServer(const std::string& host_tool_directory);
  Result<void> CheckStatus(const cvd::Status& status, const std::string& rpc);
  Result<cvd::Version> GetServerVersion(const std::string& host_tool_directory);

  static cvd::Version GetClientVersion();
};

}  // end of namespace cuttlefish
