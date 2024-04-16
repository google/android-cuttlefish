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

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/unix_sockets.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/server_constants.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

/**
 * Client to the (old) cvd servers.
 *
 * Even though cvd doesn't use a server anymore, it could encounter one (after a
 * package update, for example). This class allows talking to those servers,
 * mainly to stop them cleanly.
 */
class CvdClient {
 public:
  CvdClient(const android::base::LogSeverity verbosity,
            const std::string& server_socket_path = ServerSocketPath());

  Result<void> ConnectToServer();

  Result<void> StopCvdServer(bool clear);

  Result<void> RestartServerMatchClient();

 private:
  struct OverrideFd {
    std::optional<SharedFD> stdin_override_fd;
    std::optional<SharedFD> stdout_override_fd;
    std::optional<SharedFD> stderr_override_fd;
  };

  Result<void> SetServer(const SharedFD& server);
  Result<cvd::Response> SendRequest(const cvd::Request& request,
                                    const OverrideFd& new_control_fds = {},
                                    std::optional<SharedFD> extra_fd = {});
  Result<void> CheckStatus(const cvd::Status& status, const std::string& rpc);
  Result<cvd::Response> HandleCommand(
      const std::vector<std::string>& args,
      const std::unordered_map<std::string, std::string>& env,
      const std::vector<std::string>& selector_args,
      const OverrideFd& control_fds);

  std::optional<UnixMessageSocket> server_;
  std::string server_socket_path_;
  android::base::LogSeverity verbosity_;
};

}  // end of namespace cuttlefish
