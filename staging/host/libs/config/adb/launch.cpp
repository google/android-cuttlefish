/*
 * Copyright (C) 2018 The Android Open Source Project
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
#include "host/libs/config/adb/adb.h"

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class AdbHelper {
 public:
  INJECT(AdbHelper(const CuttlefishConfig::InstanceSpecific& instance,
                   const AdbConfig& config))
      : instance_(instance), config_(config) {}

  bool ModeEnabled(const AdbMode& mode) const {
    return config_.Modes().count(mode) > 0;
  }

  std::string ConnectorTcpArg() const {
    return "0.0.0.0:" + std::to_string(instance_.adb_host_port());
  }

  std::string ConnectorVsockArg() const {
    return "vsock:" + std::to_string(instance_.vsock_guest_cid()) + ":5555";
  }

  bool VsockTunnelEnabled() const {
    return instance_.vsock_guest_cid() > 2 && ModeEnabled(AdbMode::VsockTunnel);
  }

  bool VsockHalfTunnelEnabled() const {
    return instance_.vsock_guest_cid() > 2 &&
           ModeEnabled(AdbMode::VsockHalfTunnel);
  }

  bool TcpConnectorEnabled() const {
    bool vsock_tunnel = VsockTunnelEnabled();
    bool vsock_half_tunnel = VsockHalfTunnelEnabled();
    return config_.RunConnector() && (vsock_tunnel || vsock_half_tunnel);
  }

  bool VsockConnectorEnabled() const {
    return config_.RunConnector() && ModeEnabled(AdbMode::NativeVsock);
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
  const AdbConfig& config_;
};

class AdbConnector : public CommandSource {
 public:
  INJECT(AdbConnector(const AdbHelper& helper)) : helper_(helper) {}

  // CommandSource
  Result<std::vector<Command>> Commands() override {
    Command console_forwarder_cmd(ConsoleForwarderBinary());
    Command adb_connector(AdbConnectorBinary());
    std::set<std::string> addresses;

    if (helper_.TcpConnectorEnabled()) {
      addresses.insert(helper_.ConnectorTcpArg());
    }
    if (helper_.VsockConnectorEnabled()) {
      addresses.insert(helper_.ConnectorVsockArg());
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

  // SetupFeature
  std::string Name() const override { return "AdbConnector"; }
  bool Enabled() const override {
    return helper_.TcpConnectorEnabled() || helper_.VsockConnectorEnabled();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

  const AdbHelper& helper_;
};

class SocketVsockProxy : public CommandSource, public KernelLogPipeConsumer {
 public:
  INJECT(SocketVsockProxy(const AdbHelper& helper,
                          const CuttlefishConfig::InstanceSpecific& instance,
                          KernelLogPipeProvider& log_pipe_provider))
      : helper_(helper),
        instance_(instance),
        log_pipe_provider_(log_pipe_provider) {}

  // CommandSource
  Result<std::vector<Command>> Commands() override {
    std::vector<Command> commands;
    if (helper_.VsockTunnelEnabled()) {
      Command adb_tunnel(SocketVsockProxyBinary());
      adb_tunnel.AddParameter("-adbd_events_fd=", kernel_log_pipe_);
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
       * instance.adb_host_port()
       *
       */
      adb_tunnel.AddParameter("--server_type=tcp");
      adb_tunnel.AddParameter("--server_fd=", tcp_server_);
      adb_tunnel.AddParameter("--client_type=vsock");
      adb_tunnel.AddParameter("--client_vsock_port=6520");
      adb_tunnel.AddParameter("--client_vsock_id=", instance_.vsock_guest_cid());
      commands.emplace_back(std::move(adb_tunnel));
    }
    if (helper_.VsockHalfTunnelEnabled()) {
      Command adb_tunnel(SocketVsockProxyBinary());
      adb_tunnel.AddParameter("-adbd_events_fd=", kernel_log_pipe_);
      /*
       * This socket_vsock_proxy (a.k.a. sv proxy) runs on the host, and
       * cooperates with the adbd inside the guest. See this file:
       *  shared/device.mk, especially the line says "persist.adb.tcp.port="
       *
       * The guest adbd is listening on vsock:cid:5555 across cuttlefish
       * instances. Sv proxy faces the host adb daemon via tcp. The server type
       * should be therefore tcp, and the port should differ from instance to
       * instance and be equal to instance.adb_host_port()
       */
      adb_tunnel.AddParameter("--server_type=tcp");
      adb_tunnel.AddParameter("--server_fd=", tcp_server_);
      adb_tunnel.AddParameter("--client_type=vsock");
      adb_tunnel.AddParameter("--client_vsock_port=", 5555);
      adb_tunnel.AddParameter("--client_vsock_id=", instance_.vsock_guest_cid());
      commands.emplace_back(std::move(adb_tunnel));
    }
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "SocketVsockProxy"; }
  bool Enabled() const override {
    return helper_.VsockTunnelEnabled() || helper_.VsockHalfTunnelEnabled();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override {
    return {static_cast<SetupFeature*>(&log_pipe_provider_)};
  }
  bool Setup() override {
    tcp_server_ =
        SharedFD::SocketLocalServer(instance_.adb_host_port(), SOCK_STREAM);
    if (!tcp_server_->IsOpen()) {
      LOG(ERROR) << "Unable to create socket_vsock_proxy server socket: "
                 << tcp_server_->StrError();
      return false;
    }
    kernel_log_pipe_ = log_pipe_provider_.KernelLogPipe();
    return true;
  }

  const AdbHelper& helper_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  KernelLogPipeProvider& log_pipe_provider_;
  SharedFD kernel_log_pipe_;
  SharedFD tcp_server_;
};

}  // namespace

fruit::Component<fruit::Required<KernelLogPipeProvider, const AdbConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
LaunchAdbComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, AdbConnector>()
      .addMultibinding<CommandSource, SocketVsockProxy>()
      .addMultibinding<SetupFeature, AdbConnector>()
      .addMultibinding<KernelLogPipeConsumer, SocketVsockProxy>()
      .addMultibinding<SetupFeature, SocketVsockProxy>();
}

}  // namespace cuttlefish
