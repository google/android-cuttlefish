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

#include "host/commands/run_cvd/launch.h"

#include <android-base/logging.h>
#include <set>
#include <string>
#include <utility>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

namespace {

std::string GetAdbConnectorTcpArg(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return std::string{"0.0.0.0:"} + std::to_string(instance.host_port());
}

std::string GetAdbConnectorVsockArg(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return std::string{"vsock:"} + std::to_string(instance.vsock_guest_cid()) +
         std::string{":5555"};
}

bool AdbModeEnabled(const CuttlefishConfig& config, AdbMode mode) {
  return config.adb_mode().count(mode) > 0;
}

bool AdbVsockTunnelEnabled(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.vsock_guest_cid() > 2 &&
         AdbModeEnabled(config, AdbMode::VsockTunnel);
}

bool AdbVsockHalfTunnelEnabled(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.vsock_guest_cid() > 2 &&
         AdbModeEnabled(config, AdbMode::VsockHalfTunnel);
}

bool AdbTcpConnectorEnabled(const CuttlefishConfig& config) {
  bool vsock_tunnel = AdbVsockTunnelEnabled(config);
  bool vsock_half_tunnel = AdbVsockHalfTunnelEnabled(config);
  return config.run_adb_connector() && (vsock_tunnel || vsock_half_tunnel);
}

bool AdbVsockConnectorEnabled(const CuttlefishConfig& config) {
  return config.run_adb_connector() &&
         AdbModeEnabled(config, AdbMode::NativeVsock);
}

}  // namespace

std::vector<Command> LaunchAdbConnectorIfEnabled(
    const CuttlefishConfig& config) {
  Command adb_connector(AdbConnectorBinary());
  std::set<std::string> addresses;

  if (AdbTcpConnectorEnabled(config)) {
    addresses.insert(GetAdbConnectorTcpArg(config));
  }
  if (AdbVsockConnectorEnabled(config)) {
    addresses.insert(GetAdbConnectorVsockArg(config));
  }

  if (addresses.size() == 0) {
    return {};
  }
  std::string address_arg = "--addresses=";
  for (auto& arg : addresses) {
    address_arg += arg + ",";
  }
  address_arg.pop_back();
  adb_connector.AddParameter(address_arg);
  std::vector<Command> commands;
  commands.emplace_back(std::move(adb_connector));
  return std::move(commands);
}

std::vector<Command> LaunchSocketVsockProxyIfEnabled(
    const CuttlefishConfig& config, SharedFD adbd_events_pipe) {
  auto instance = config.ForDefaultInstance();
  auto append = [](const std::string& s, const int i) -> std::string {
    return s + std::to_string(i);
  };
  auto tcp_server =
      SharedFD::SocketLocalServer(instance.host_port(), SOCK_STREAM);
  CHECK(tcp_server->IsOpen())
      << "Unable to create socket_vsock_proxy server socket: "
      << tcp_server->StrError();
  std::vector<Command> commands;
  if (AdbVsockTunnelEnabled(config)) {
    Command adb_tunnel(SocketVsockProxyBinary());
    adb_tunnel.AddParameter("-adbd_events_fd=", adbd_events_pipe);
    /**
     * This socket_vsock_proxy (a.k.a. sv proxy) runs on the host. It assumes
     * that another sv proxy runs inside the guest. see:
     * shared/config/init.vendor.rc The sv proxy in the guest exposes
     * vsock:cid:6520 across the cuttlefish instances in multi-tenancy. cid is
     * different per instance.
     *
     * This host sv proxy should cooperate with the guest sv proxy. Thus, one
     * end of the tunnel is vsock:cid:6520 regardless of instance number.
     * Another end faces the host adb daemon via tcp. Thus, the server type is
     * tcp here. The tcp port differs from instance to instance, and is
     * instance.host_port()
     *
     */
    adb_tunnel.AddParameter("--server=tcp");
    adb_tunnel.AddParameter("--vsock_port=6520");
    adb_tunnel.AddParameter(std::string{"--server_fd="}, tcp_server);
    adb_tunnel.AddParameter(std::string{"--vsock_cid="} +
                            std::to_string(instance.vsock_guest_cid()));
    commands.emplace_back(std::move(adb_tunnel));
  }
  if (AdbVsockHalfTunnelEnabled(config)) {
    Command adb_tunnel(SocketVsockProxyBinary());
    adb_tunnel.AddParameter("-adbd_events_fd=", adbd_events_pipe);
    /*
     * This socket_vsock_proxy (a.k.a. sv proxy) runs on the host, and
     * cooperates with the adbd inside the guest. See this file:
     *  shared/device.mk, especially the line says "persist.adb.tcp.port="
     *
     * The guest adbd is listening on vsock:cid:5555 across cuttlefish
     * instances. Sv proxy faces the host adb daemon via tcp. The server type
     * should be therefore tcp, and the port should differ from instance to
     * instance and be equal to instance.host_port()
     */
    adb_tunnel.AddParameter("--server=tcp");
    adb_tunnel.AddParameter(append("--vsock_port=", 5555));
    adb_tunnel.AddParameter(std::string{"--server_fd="}, tcp_server);
    adb_tunnel.AddParameter(append("--vsock_cid=", instance.vsock_guest_cid()));
    commands.emplace_back(std::move(adb_tunnel));
  }
  return commands;
}

}  // namespace cuttlefish
