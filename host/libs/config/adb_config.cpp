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
#include "host/libs/config/adb_config.h"

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <fruit/fruit.h>
#include <algorithm>
#include <set>

#include "common/libs/utils/environment.h"
#include "host/libs/config/config_flag.h"
#include "host/libs/config/config_fragment.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {
namespace {

class AdbConfigImpl : public AdbConfig {
 public:
  INJECT(AdbConfigImpl()) {}

  const std::set<AdbMode>& Modes() const override { return modes_; }
  bool SetModes(const std::set<AdbMode>& modes) override {
    modes_ = modes;
    return true;
  }
  bool SetModes(std::set<AdbMode>&& modes) override {
    modes_ = std::move(modes);
    return true;
  }

  bool RunConnector() const override { return run_connector_; }
  bool SetRunConnector(bool run) override {
    run_connector_ = run;
    return true;
  }

 private:
  std::set<AdbMode> modes_;
  bool run_connector_;
};

AdbMode StringToAdbMode(std::string mode) {
  std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
  if (mode == "vsock_tunnel") {
    return AdbMode::VsockTunnel;
  } else if (mode == "vsock_half_tunnel") {
    return AdbMode::VsockHalfTunnel;
  } else if (mode == "native_vsock") {
    return AdbMode::NativeVsock;
  } else {
    return AdbMode::Unknown;
  }
}

std::string AdbModeToString(AdbMode mode) {
  switch (mode) {
    case AdbMode::VsockTunnel:
      return "vsock_tunnel";
    case AdbMode::VsockHalfTunnel:
      return "vsock_half_tunnel";
    case AdbMode::NativeVsock:
      return "native_vsock";
    case AdbMode::Unknown:  // fall through
    default:
      return "unknown";
  }
}

class AdbConfigFragmentImpl : public AdbConfigFragment {
 public:
  INJECT(AdbConfigFragmentImpl(AdbConfig& config)) : config_(config) {}

  std::string Name() const override { return "AdbConfigFragmentImpl"; }

  Json::Value Serialize() const override {
    Json::Value json;
    json[kMode] = Json::Value(Json::arrayValue);
    for (const auto& mode : config_.Modes()) {
      json[kMode].append(AdbModeToString(mode));
    }
    json[kConnectorEnabled] = config_.RunConnector();
    return json;
  }
  bool Deserialize(const Json::Value& json) override {
    if (!json.isMember(kMode) || json[kMode].type() != Json::arrayValue) {
      LOG(ERROR) << "Invalid value for " << kMode;
      return false;
    }
    std::set<AdbMode> modes;
    for (auto& mode : json[kMode]) {
      if (mode.type() != Json::stringValue) {
        LOG(ERROR) << "Invalid mode type" << mode;
        return false;
      }
      modes.insert(StringToAdbMode(mode.asString()));
    }
    if (!config_.SetModes(std::move(modes))) {
      LOG(ERROR) << "Failed to set adb modes";
      return false;
    }

    if (!json.isMember(kConnectorEnabled) ||
        json[kConnectorEnabled].type() != Json::booleanValue) {
      LOG(ERROR) << "Invalid value for " << kConnectorEnabled;
      return false;
    }
    if (!config_.SetRunConnector(json[kConnectorEnabled].asBool())) {
      LOG(ERROR) << "Failed to set whether to run the adb connector";
    }
    return true;
  }

 private:
  static constexpr char kMode[] = "mode";
  static constexpr char kConnectorEnabled[] = "connector_enabled";
  AdbConfig& config_;
};

class AdbConfigFlagImpl : public AdbConfigFlag {
 public:
  INJECT(AdbConfigFlagImpl(AdbConfig& config, ConfigFlag& config_flag))
      : config_(config), config_flag_(config_flag) {
    mode_flag_ = GflagsCompatFlag("adb_mode").Help(mode_help);
    mode_flag_.Getter([this]() {
      std::stringstream modes;
      for (const auto& mode : config_.Modes()) {
        modes << "," << AdbModeToString(mode);
      }
      return modes.str().substr(1);  // First comma
    });
    mode_flag_.Setter([this](const FlagMatch& match) {
      // TODO(schuffelen): Error on unknown types?
      std::set<AdbMode> modes;
      for (auto& mode : android::base::Split(match.value, ",")) {
        modes.insert(StringToAdbMode(mode));
      }
      return config_.SetModes(modes);
    });
  }

  std::string Name() const override { return "AdbConfigFlagImpl"; }

  std::unordered_set<FlagFeature*> Dependencies() const override {
    return {static_cast<FlagFeature*>(&config_flag_)};
  }

  bool Process(std::vector<std::string>& args) override {
    // Defaults
    config_.SetModes({AdbMode::VsockHalfTunnel});
    bool run_adb_connector = !IsRunningInContainer();
    Flag run_flag = GflagsCompatFlag("run_adb_connector", run_adb_connector);
    if (!ParseFlags({run_flag, mode_flag_}, args)) {
      LOG(ERROR) << "Failed to parse adb config flags";
      return false;
    }
    config_.SetRunConnector(run_adb_connector);

    auto adb_modes_check = config_.Modes();
    adb_modes_check.erase(AdbMode::Unknown);
    if (adb_modes_check.size() < 1) {
      LOG(INFO) << "ADB not enabled";
    }

    return true;
  }
  bool WriteGflagsCompatHelpXml(std::ostream& out) const override {
    bool run = config_.RunConnector();
    Flag run_flag = GflagsCompatFlag("run_adb_connector", run).Help(run_help);
    return WriteGflagsCompatXml({run_flag, mode_flag_}, out);
  }

 private:
  static constexpr char run_help[] =
      "Maintain adb connection by sending 'adb connect' commands to the "
      "server. Only relevant with -adb_mode=tunnel or vsock_tunnel.";
  static constexpr char mode_help[] =
      "Mode for ADB connection."
      "'vsock_tunnel' for a TCP connection tunneled through vsock, "
      "'native_vsock' for a  direct connection to the guest ADB over "
      "vsock, 'vsock_half_tunnel' for a TCP connection forwarded to "
      "the guest ADB server, or a comma separated list of types as in "
      "'native_vsock,vsock_half_tunnel'";

  AdbConfig& config_;
  ConfigFlag& config_flag_;
  Flag mode_flag_;
};

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
  std::vector<Command> Commands() override {
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

  // Feature
  bool Enabled() const override {
    return helper_.TcpConnectorEnabled() || helper_.VsockConnectorEnabled();
  }
  std::string Name() const override { return "AdbConnector"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override { return true; }

 private:
  const AdbHelper& helper_;
};

class SocketVsockProxy : public CommandSource {
 public:
  INJECT(SocketVsockProxy(const AdbHelper& helper,
                          const CuttlefishConfig::InstanceSpecific& instance,
                          KernelLogPipeProvider& log_pipe_provider))
      : helper_(helper),
        instance_(instance),
        log_pipe_provider_(log_pipe_provider) {}

  // CommandSource
  std::vector<Command> Commands() override {
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
      adb_tunnel.AddParameter("--server=tcp");
      adb_tunnel.AddParameter("--vsock_port=6520");
      adb_tunnel.AddParameter("--server_fd=", tcp_server_);
      adb_tunnel.AddParameter("--vsock_cid=", instance_.vsock_guest_cid());
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
      adb_tunnel.AddParameter("--server=tcp");
      adb_tunnel.AddParameter("--vsock_port=", 5555);
      adb_tunnel.AddParameter("--server_fd=", tcp_server_);
      adb_tunnel.AddParameter("--vsock_cid=", instance_.vsock_guest_cid());
      commands.emplace_back(std::move(adb_tunnel));
    }
    return commands;
  }

  // Feature
  bool Enabled() const override {
    return helper_.VsockTunnelEnabled() || helper_.VsockHalfTunnelEnabled();
  }
  std::string Name() const override { return "SocketVsockProxy"; }
  std::unordered_set<Feature*> Dependencies() const override {
    return {static_cast<Feature*>(&log_pipe_provider_)};
  }

 protected:
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

 private:
  const AdbHelper& helper_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  KernelLogPipeProvider& log_pipe_provider_;
  SharedFD kernel_log_pipe_;
  SharedFD tcp_server_;
};

}  // namespace

fruit::Component<AdbConfig> AdbConfigComponent() {
  return fruit::createComponent().bind<AdbConfig, AdbConfigImpl>();
}
fruit::Component<fruit::Required<AdbConfig, ConfigFlag>, AdbConfigFlag>
AdbConfigFlagComponent() {
  return fruit::createComponent()
      .bind<AdbConfigFlag, AdbConfigFlagImpl>()
      .addMultibinding<FlagFeature, AdbConfigFlag>();
}
fruit::Component<fruit::Required<AdbConfig>, AdbConfigFragment>
AdbConfigFragmentComponent() {
  return fruit::createComponent()
      .bind<AdbConfigFragment, AdbConfigFragmentImpl>()
      .addMultibinding<ConfigFragment, AdbConfigFragment>();
}

fruit::Component<fruit::Required<KernelLogPipeProvider, const AdbConfig,
                                 const CuttlefishConfig::InstanceSpecific>>
LaunchAdbComponent() {
  return fruit::createComponent()
      .addMultibinding<CommandSource, AdbConnector>()
      .addMultibinding<CommandSource, SocketVsockProxy>()
      .addMultibinding<Feature, AdbConnector>()
      .addMultibinding<Feature, SocketVsockProxy>();
}

}  // namespace cuttlefish
