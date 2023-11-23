//
// Copyright (C) 2019 The Android Open Source Project
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

#include "host/commands/run_cvd/launch/launch.h"

#include "common/libs/utils/result.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<MonitorCommand> ConfigServer(
    const CuttlefishConfig::InstanceSpecific& instance) {
  auto port = instance.config_server_port();
  auto socket =
      SharedFD::VsockServer(port, SOCK_STREAM,
                            instance.vhost_user_vsock()
                                ? std::make_optional(instance.vsock_guest_cid())
                                : std::nullopt);
  CF_EXPECTF(socket->IsOpen(), "Can't configuration server socket: '{}'",
             socket->StrError());
  return Command(ConfigServerBinary()).AddParameter("-server_fd=", socket);
}

}  // namespace cuttlefish
