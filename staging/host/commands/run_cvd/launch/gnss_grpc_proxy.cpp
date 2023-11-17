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
#include <vector>

#include <fruit/fruit.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/command_source.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

Result<std::optional<MonitorCommand>> GnssGrpcProxyServer(
    const CuttlefishConfig::InstanceSpecific& instance,
    GrpcSocketCreator& grpc_socket) {
  if (!instance.enable_gnss_grpc_proxy()) {
    return {};
  }
  std::vector<SharedFD> fifos;
  std::vector<std::string> fifo_paths = {
      instance.PerInstanceInternalPath("gnsshvc_fifo_vm.in"),
      instance.PerInstanceInternalPath("gnsshvc_fifo_vm.out"),
      instance.PerInstanceInternalPath("locationhvc_fifo_vm.in"),
      instance.PerInstanceInternalPath("locationhvc_fifo_vm.out"),
  };
  for (const auto& path : fifo_paths) {
    fifos.emplace_back(CF_EXPECT(SharedFD::Fifo(path, 0660)));
  }

  auto gnss_grpc_proxy_cmd =
      Command(GnssGrpcProxyBinary())
          .AddParameter("--gnss_in_fd=", fifos[0])
          .AddParameter("--gnss_out_fd=", fifos[1])
          .AddParameter("--fixed_location_in_fd=", fifos[2])
          .AddParameter("--fixed_location_out_fd=", fifos[3])
          .AddParameter("--gnss_grpc_port=",
                        instance.gnss_grpc_proxy_server_port())
          .AddParameter("--gnss_grpc_socket=",
                        grpc_socket.CreateGrpcSocket("GnssGrpcProxyServer"));
  if (!instance.gnss_file_path().empty()) {
    // If path is provided, proxy will start as local mode.
    gnss_grpc_proxy_cmd.AddParameter("--gnss_file_path=",
                                     instance.gnss_file_path());
  }
  if (!instance.fixed_location_file_path().empty()) {
    gnss_grpc_proxy_cmd.AddParameter("--fixed_location_file_path=",
                                     instance.fixed_location_file_path());
  }
  return gnss_grpc_proxy_cmd;
}

}  // namespace cuttlefish
