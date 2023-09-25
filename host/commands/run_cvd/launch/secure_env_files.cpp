//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/run_cvd/launch/secure_env_files.h"

#include <unistd.h>

namespace cuttlefish {

SecureEnvFiles::SecureEnvFiles(
    const CuttlefishConfig::InstanceSpecific& instance)
    : instance_(instance) {}

Result<void> SecureEnvFiles::ResultSetup() {
  auto confui_socket_path =
      instance_.PerInstanceInternalUdsPath("confui_sign.sock");
  unlink(confui_socket_path.c_str());
  confui_server_fd_ =
      SharedFD::SocketLocalServer(confui_socket_path, false, SOCK_STREAM, 0600);
  CF_EXPECTF(confui_server_fd_->IsOpen(), "Could not open \"{}\": {}",
             confui_socket_path, confui_server_fd_->StrError());

  SharedFD::SocketPair(AF_UNIX, SOCK_STREAM, 0, &snapshot_control_fd_,
                       &run_cvd_to_secure_env_fd_);

  return {};
}

fruit::Component<fruit::Required<const CuttlefishConfig::InstanceSpecific>,
                 SecureEnvFiles>
SecureEnvFilesComponent() {
  return fruit::createComponent()
      .addMultibinding<SetupFeature, SecureEnvFiles>();
}

}  // namespace cuttlefish
