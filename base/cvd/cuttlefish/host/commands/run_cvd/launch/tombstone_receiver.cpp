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

#include <string>

#include <fruit/fruit.h>

#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<MonitorCommand> TombstoneReceiver(
    const CuttlefishConfig::InstanceSpecific& instance) {
  auto tombstone_dir = instance.PerInstancePath("tombstones");
  if (!DirectoryExists(tombstone_dir)) {
    LOG(DEBUG) << "Setting up " << tombstone_dir;
    CF_EXPECTF(mkdir(tombstone_dir.c_str(),
                     S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0,
               "Failed to create tombstone directory: '{}'. error: '{}'",
               tombstone_dir, strerror(errno));
  }

  auto port = instance.tombstone_receiver_port();
  auto socket =
      SharedFD::VsockServer(port, SOCK_STREAM,
                            instance.vhost_user_vsock()
                                ? std::make_optional(instance.vsock_guest_cid())
                                : std::nullopt);
  CF_EXPECTF(socket->IsOpen(), "Can't tombstone server socket: '{}'",
             socket->StrError());

  return Command(TombstoneReceiverBinary())
      .AddParameter("-server_fd=", socket)
      .AddParameter("-tombstone_dir=", tombstone_dir);
}

}  // namespace cuttlefish
