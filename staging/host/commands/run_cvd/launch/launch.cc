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

#include <android-base/logging.h>

#include <unordered_set>
#include <utility>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/network.h"
#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/commands/run_cvd/reporting.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/inject.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/crosvm_builder.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/vm_manager.h"

namespace cuttlefish {

using vm_manager::VmManager;

class LogTeeCreator {
 public:
  INJECT(LogTeeCreator(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  Command CreateLogTee(Command& cmd, const std::string& process_name) {
    auto name_with_ext = process_name + "_logs.fifo";
    auto logs_path = instance_.PerInstanceInternalPath(name_with_ext.c_str());
    auto logs = SharedFD::Fifo(logs_path, 0666);
    if (!logs->IsOpen()) {
      LOG(FATAL) << "Failed to create fifo for " << process_name
                 << " output: " << logs->StrError();
    }

    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdOut, logs);
    cmd.RedirectStdIO(Subprocess::StdIOChannel::kStdErr, logs);

    return Command(HostBinaryPath("log_tee"))
        .AddParameter("--process_name=", process_name)
        .AddParameter("--log_fd_in=", logs);
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class RootCanal : public CommandSource {
 public:
  INJECT(RootCanal(const CuttlefishConfig& config,
                   const CuttlefishConfig::InstanceSpecific& instance,
                   LogTeeCreator& log_tee))
      : config_(config), instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  std::vector<Command> Commands() override {
    if (!Enabled()) {
      return {};
    }
    Command command(RootCanalBinary());

    // Test port
    command.AddParameter(config_.rootcanal_test_port());
    // HCI server port
    command.AddParameter(config_.rootcanal_hci_port());
    // Link server port
    command.AddParameter(config_.rootcanal_link_port());
    // Bluetooth controller properties file
    command.AddParameter("--controller_properties_file=",
                         config_.rootcanal_config_file());
    // Default commands file
    command.AddParameter("--default_commands_file=",
                         config_.rootcanal_default_commands_file());

    std::vector<Command> commands;
    commands.emplace_back(log_tee_.CreateLogTee(command, "rootcanal"));
    commands.emplace_back(std::move(command));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "RootCanal"; }
  bool Enabled() const override {
    return config_.enable_host_bluetooth() && instance_.start_rootcanal();
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
};

class MetricsService : public CommandSource {
 public:
  INJECT(MetricsService(const CuttlefishConfig& config)) : config_(config) {}

  // CommandSource
  std::vector<Command> Commands() override {
    return single_element_emplace(Command(MetricsBinary()));
  }

  // SetupFeature
  std::string Name() const override { return "MetricsService"; }
  bool Enabled() const override {
    return config_.enable_metrics() == CuttlefishConfig::kYes;
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

 private:
  const CuttlefishConfig& config_;
};

class GnssGrpcProxyServer : public CommandSource {
 public:
  INJECT(
      GnssGrpcProxyServer(const CuttlefishConfig& config,
                          const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // CommandSource
  std::vector<Command> Commands() override {
    Command gnss_grpc_proxy_cmd(GnssGrpcProxyBinary());
    const unsigned gnss_grpc_proxy_server_port =
        instance_.gnss_grpc_proxy_server_port();
    gnss_grpc_proxy_cmd.AddParameter("--gnss_in_fd=", gnss_grpc_proxy_in_wr_);
    gnss_grpc_proxy_cmd.AddParameter("--gnss_out_fd=", gnss_grpc_proxy_out_rd_);
    gnss_grpc_proxy_cmd.AddParameter("--fixed_location_in_fd=",
                                     fixed_location_grpc_proxy_in_wr_);
    gnss_grpc_proxy_cmd.AddParameter("--fixed_location_out_fd=",
                                     fixed_location_grpc_proxy_out_rd_);
    gnss_grpc_proxy_cmd.AddParameter("--gnss_grpc_port=",
                                     gnss_grpc_proxy_server_port);
    if (!instance_.gnss_file_path().empty()) {
      // If path is provided, proxy will start as local mode.
      gnss_grpc_proxy_cmd.AddParameter("--gnss_file_path=",
                                       instance_.gnss_file_path());
    }
    if (!instance_.fixed_location_file_path().empty()) {
      gnss_grpc_proxy_cmd.AddParameter("--fixed_location_file_path=",
                                       instance_.fixed_location_file_path());
    }
    return single_element_emplace(std::move(gnss_grpc_proxy_cmd));
  }

  // SetupFeature
  std::string Name() const override { return "GnssGrpcProxyServer"; }
  bool Enabled() const override {
    return config_.enable_gnss_grpc_proxy() &&
           FileExists(GnssGrpcProxyBinary());
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    std::vector<SharedFD> fifos;
    std::vector<std::string> fifo_paths = {
        instance_.PerInstanceInternalPath("gnsshvc_fifo_vm.in"),
        instance_.PerInstanceInternalPath("gnsshvc_fifo_vm.out"),
        instance_.PerInstanceInternalPath("locationhvc_fifo_vm.in"),
        instance_.PerInstanceInternalPath("locationhvc_fifo_vm.out"),
    };
    for (const auto& path : fifo_paths) {
      unlink(path.c_str());
      CF_EXPECT(mkfifo(path.c_str(), 0660) == 0, "Could not create " << path);
      auto fd = SharedFD::Open(path, O_RDWR);
      CF_EXPECT(fd->IsOpen(),
                "Could not open " << path << ": " << fd->StrError());
      fifos.push_back(fd);
    }

    gnss_grpc_proxy_in_wr_ = fifos[0];
    gnss_grpc_proxy_out_rd_ = fifos[1];
    fixed_location_grpc_proxy_in_wr_ = fifos[2];
    fixed_location_grpc_proxy_out_rd_ = fifos[3];
    return {};
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD gnss_grpc_proxy_in_wr_;
  SharedFD gnss_grpc_proxy_out_rd_;
  SharedFD fixed_location_grpc_proxy_in_wr_;
  SharedFD fixed_location_grpc_proxy_out_rd_;
};

class VehicleHalServer : public CommandSource {
 public:
  INJECT(VehicleHalServer(const CuttlefishConfig& config,
                          const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // CommandSource
  std::vector<Command> Commands() override {
    Command grpc_server(VehicleHalGrpcServerBinary());

    const unsigned vhal_server_cid = 2;
    const unsigned vhal_server_port = instance_.vehicle_hal_server_port();
    const std::string vhal_server_power_state_file =
        AbsolutePath(instance_.PerInstancePath("power_state"));
    const std::string vhal_server_power_state_socket =
        AbsolutePath(instance_.PerInstancePath("power_state_socket"));

    grpc_server.AddParameter("--server_cid=", vhal_server_cid);
    grpc_server.AddParameter("--server_port=", vhal_server_port);
    grpc_server.AddParameter("--power_state_file=",
                             vhal_server_power_state_file);
    grpc_server.AddParameter("--power_state_socket=",
                             vhal_server_power_state_socket);
    return single_element_emplace(std::move(grpc_server));
  }

  // SetupFeature
  std::string Name() const override { return "VehicleHalServer"; }
  bool Enabled() const override {
    return config_.enable_vehicle_hal_grpc_server() &&
           FileExists(VehicleHalGrpcServerBinary());
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class ConsoleForwarder : public CommandSource, public DiagnosticInformation {
 public:
  INJECT(ConsoleForwarder(const CuttlefishConfig& config,
                          const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}
  // DiagnosticInformation
  std::vector<std::string> Diagnostics() const override {
    if (Enabled()) {
      return {"To access the console run: screen " + instance_.console_path()};
    } else {
      return {"Serial console is disabled; use -console=true to enable it."};
    }
  }

  // CommandSource
  std::vector<Command> Commands() override {
    Command console_forwarder_cmd(ConsoleForwarderBinary());

    console_forwarder_cmd.AddParameter("--console_in_fd=",
                                       console_forwarder_in_wr_);
    console_forwarder_cmd.AddParameter("--console_out_fd=",
                                       console_forwarder_out_rd_);
    return single_element_emplace(std::move(console_forwarder_cmd));
  }

  // SetupFeature
  std::string Name() const override { return "ConsoleForwarder"; }
  bool Enabled() const override { return config_.console(); }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    auto console_in_pipe_name = instance_.console_in_pipe_name();
    CF_EXPECT(
        mkfifo(console_in_pipe_name.c_str(), 0600) == 0,
        "Failed to create console input fifo for crosvm: " << strerror(errno));

    auto console_out_pipe_name = instance_.console_out_pipe_name();
    CF_EXPECT(
        mkfifo(console_out_pipe_name.c_str(), 0660) == 0,
        "Failed to create console output fifo for crosvm: " << strerror(errno));

    // These fds will only be read from or written to, but open them with
    // read and write access to keep them open in case the subprocesses exit
    console_forwarder_in_wr_ =
        SharedFD::Open(console_in_pipe_name.c_str(), O_RDWR);
    CF_EXPECT(console_forwarder_in_wr_->IsOpen(),
              "Failed to open console_forwarder input fifo for writes: "
                  << console_forwarder_in_wr_->StrError());

    console_forwarder_out_rd_ =
        SharedFD::Open(console_out_pipe_name.c_str(), O_RDWR);
    CF_EXPECT(console_forwarder_out_rd_->IsOpen(),
              "Failed to open console_forwarder output fifo for reads: "
                  << console_forwarder_out_rd_->StrError());
    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD console_forwarder_in_wr_;
  SharedFD console_forwarder_out_rd_;
};

class WmediumdServer : public CommandSource {
 public:
  INJECT(WmediumdServer(const CuttlefishConfig& config,
                        const CuttlefishConfig::InstanceSpecific& instance,
                        LogTeeCreator& log_tee))
      : config_(config), instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  std::vector<Command> Commands() override {
    Command cmd(WmediumdBinary());
    cmd.AddParameter("-u", config_.vhost_user_mac80211_hwsim());
    cmd.AddParameter("-a", config_.wmediumd_api_server_socket());
    cmd.AddParameter("-c", config_path_);

    std::vector<Command> commands;
    commands.emplace_back(log_tee_.CreateLogTee(cmd, "wmediumd"));
    commands.emplace_back(std::move(cmd));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "WmediumdServer"; }
  bool Enabled() const override {
#ifndef ENFORCE_MAC80211_HWSIM
    return false;
#else
    return instance_.start_wmediumd();
#endif
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  Result<void> ResultSetup() override {
    // If wmediumd configuration is given, use it
    if (!config_.wmediumd_config().empty()) {
      config_path_ = config_.wmediumd_config();
      return {};
    }
    // Otherwise, generate wmediumd configuration using the current wifi mac
    // prefix before start
    config_path_ = instance_.PerInstanceInternalPath("wmediumd.cfg");
    Command gen_config_cmd(WmediumdGenConfigBinary());
    gen_config_cmd.AddParameter("-o", config_path_);
    gen_config_cmd.AddParameter("-p", instance_.wifi_mac_prefix());

    int success = gen_config_cmd.Start().Wait();
    CF_EXPECT(success == 0, "Unable to run " << gen_config_cmd.Executable()
                                             << ". Exited with status "
                                             << success);
    return {};
  }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
  std::string config_path_;
};

class VmmCommands : public CommandSource {
 public:
  INJECT(VmmCommands(const CuttlefishConfig& config, VmManager& vmm))
      : config_(config), vmm_(vmm) {}

  // CommandSource
  std::vector<Command> Commands() override {
    return vmm_.StartCommands(config_);
  }

  // SetupFeature
  std::string Name() const override { return "VirtualMachineManager"; }
  bool Enabled() const override { return true; }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

  const CuttlefishConfig& config_;
  VmManager& vmm_;
};

class OpenWrt : public CommandSource {
 public:
  INJECT(OpenWrt(const CuttlefishConfig& config,
                 const CuttlefishConfig::InstanceSpecific& instance,
                 LogTeeCreator& log_tee))
      : config_(config), instance_(instance), log_tee_(log_tee) {}

  // CommandSource
  std::vector<Command> Commands() override {
    constexpr auto crosvm_for_ap_socket = "ap_control.sock";

    CrosvmBuilder ap_cmd;
    ap_cmd.SetBinary(config_.crosvm_binary());
    ap_cmd.AddControlSocket(
        instance_.PerInstanceInternalPath(crosvm_for_ap_socket));

    if (!config_.vhost_user_mac80211_hwsim().empty()) {
      ap_cmd.Cmd().AddParameter("--vhost-user-mac80211-hwsim=",
                                config_.vhost_user_mac80211_hwsim());
    }
    SharedFD wifi_tap = ap_cmd.AddTap(instance_.wifi_tap_name());
    // Only run the leases workaround if we are not using the new network
    // bridge architecture - in that case, we have a wider DHCP address
    // space and stale leases should be much less of an issue
    if (!FileExists("/var/run/cuttlefish-dnsmasq-cvd-wbr.leases") &&
        wifi_tap->IsOpen()) {
      // TODO(schuffelen): QEMU also needs this and this is not the best place
      // for this code. Find a better place to put it.
      auto lease_file =
          ForCurrentInstance("/var/run/cuttlefish-dnsmasq-cvd-wbr-") +
          ".leases";
      std::uint8_t dhcp_server_ip[] = {
          192, 168, 96, (std::uint8_t)(ForCurrentInstance(1) * 4 - 3)};
      if (!ReleaseDhcpLeases(lease_file, wifi_tap, dhcp_server_ip)) {
        LOG(ERROR)
            << "Failed to release wifi DHCP leases. Connecting to the wifi "
            << "network may not work.";
      }
    }
    if (config_.enable_sandbox()) {
      ap_cmd.Cmd().AddParameter("--seccomp-policy-dir=",
                                config_.seccomp_policy_dir());
    } else {
      ap_cmd.Cmd().AddParameter("--disable-sandbox");
    }
    ap_cmd.Cmd().AddParameter("--rwdisk=",
                              instance_.PerInstancePath("ap_overlay.img"));
    ap_cmd.Cmd().AddParameter(
        "--disk=", instance_.PerInstancePath("persistent_composite.img"));
    ap_cmd.Cmd().AddParameter("--params=\"root=" + config_.ap_image_dev_path() +
                              "\"");

    auto boot_logs_path =
        instance_.PerInstanceLogPath("crosvm_openwrt_boot.log");
    auto logs_path = instance_.PerInstanceLogPath("crosvm_openwrt.log");
    ap_cmd.AddSerialConsoleReadOnly(boot_logs_path);
    ap_cmd.AddHvcReadOnly(logs_path);

    ap_cmd.Cmd().AddParameter(config_.ap_kernel_image());

    std::vector<Command> commands;
    commands.emplace_back(log_tee_.CreateLogTee(ap_cmd.Cmd(), "openwrt"));
    commands.emplace_back(std::move(ap_cmd.Cmd()));
    return commands;
  }

  // SetupFeature
  std::string Name() const override { return "OpenWrt"; }
  bool Enabled() const override {
#ifndef ENFORCE_MAC80211_HWSIM
    return false;
#else
    return instance_.start_ap() &&
           config_.vm_manager() == vm_manager::CrosvmManager::name();
#endif
  }

 private:
  std::unordered_set<SetupFeature*> Dependencies() const override { return {}; }
  bool Setup() override { return true; }

  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  LogTeeCreator& log_tee_;
};

using PublicDeps = fruit::Required<const CuttlefishConfig, VmManager,
                                   const CuttlefishConfig::InstanceSpecific>;
fruit::Component<PublicDeps, KernelLogPipeProvider> launchComponent() {
  using InternalDeps = fruit::Required<const CuttlefishConfig, VmManager,
                                       const CuttlefishConfig::InstanceSpecific,
                                       KernelLogPipeProvider>;
  using Multi = Multibindings<InternalDeps>;
  using Bases = Multi::Bases<CommandSource, DiagnosticInformation, SetupFeature,
                             LateInjected, KernelLogPipeConsumer>;
  return fruit::createComponent()
      .install(BluetoothConnectorComponent)
      .install(ConfigServerComponent)
      .install(LogcatReceiverComponent)
      .install(KernelLogMonitorComponent)
      .install(SecureEnvComponent)
      .install(TombstoneReceiverComponent)
      .install(Bases::Impls<ConsoleForwarder>)
      .install(Bases::Impls<GnssGrpcProxyServer>)
      .install(Bases::Impls<MetricsService>)
      .install(Bases::Impls<RootCanal>)
      .install(Bases::Impls<VehicleHalServer>)
      .install(Bases::Impls<VmmCommands>)
      .install(Bases::Impls<WmediumdServer>)
      .install(Bases::Impls<OpenWrt>);
}

}  // namespace cuttlefish
