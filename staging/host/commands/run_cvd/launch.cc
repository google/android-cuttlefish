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
#include <unordered_set>
#include <utility>
#include <vector>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/commands/run_cvd/reporting.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/inject.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

namespace {

template <typename T>
std::vector<T> single_element_emplace(T&& element) {
  std::vector<T> vec;
  vec.emplace_back(std::move(element));
  return vec;
}

}  // namespace

CommandSource::~CommandSource() = default;

KernelLogPipeProvider::~KernelLogPipeProvider() = default;

class KernelLogMonitor : public CommandSource,
                         public KernelLogPipeProvider,
                         public DiagnosticInformation {
 public:
  INJECT(KernelLogMonitor(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // DiagnosticInformation
  std::vector<std::string> Diagnostics() const override {
    return {"Kernel log: " + instance_.PerInstancePath("kernel.log")};
  }

  // CommandSource
  std::vector<Command> Commands() override {
    Command command(KernelLogMonitorBinary());
    command.AddParameter("-log_pipe_fd=", fifo_);

    if (!event_pipe_write_ends_.empty()) {
      command.AddParameter("-subscriber_fds=");
      for (size_t i = 0; i < event_pipe_write_ends_.size(); i++) {
        if (i > 0) {
          command.AppendToLastParameter(",");
        }
        command.AppendToLastParameter(event_pipe_write_ends_[i]);
      }
    }

    return single_element_emplace(std::move(command));
  }

  // KernelLogPipeProvider
  SharedFD KernelLogPipe() override {
    CHECK(!event_pipe_read_ends_.empty()) << "No more kernel pipes left";
    SharedFD ret = event_pipe_read_ends_.back();
    event_pipe_read_ends_.pop_back();
    return ret;
  }

  // Feature
  bool Enabled() const override { return true; }
  std::string Name() const override { return "KernelLogMonitor"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override {
    auto log_name = instance_.kernel_log_pipe_name();
    if (mkfifo(log_name.c_str(), 0600) != 0) {
      LOG(ERROR) << "Unable to create named pipe at " << log_name << ": "
                 << strerror(errno);
      return false;
    }

    // Open the pipe here (from the launcher) to ensure the pipe is not deleted
    // due to the usage counters in the kernel reaching zero. If this is not
    // done and the kernel_log_monitor crashes for some reason the VMM may get
    // SIGPIPE.
    fifo_ = SharedFD::Open(log_name, O_RDWR);
    if (!fifo_->IsOpen()) {
      LOG(ERROR) << "Unable to open \"" << log_name << "\"";
      return false;
    }

    // TODO(schuffelen): Find a way to calculate this dynamically.
    int number_of_event_pipes = 4;
    if (number_of_event_pipes > 0) {
      for (unsigned int i = 0; i < number_of_event_pipes; ++i) {
        SharedFD event_pipe_write_end, event_pipe_read_end;
        if (!SharedFD::Pipe(&event_pipe_read_end, &event_pipe_write_end)) {
          PLOG(ERROR) << "Unable to create kernel log events pipe: ";
          return false;
        }
        event_pipe_write_ends_.push_back(event_pipe_write_end);
        event_pipe_read_ends_.push_back(event_pipe_read_end);
      }
    }
    return true;
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD fifo_;
  std::vector<SharedFD> event_pipe_write_ends_;
  std::vector<SharedFD> event_pipe_read_ends_;
};

class RootCanal : public CommandSource {
 public:
  INJECT(RootCanal(const CuttlefishConfig& config,
                   const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // CommandSource
  std::vector<Command> Commands() override {
    if (!Enabled()) {
      return {};
    }
    Command command(RootCanalBinary());

    // Test port
    command.AddParameter(instance_.rootcanal_test_port());
    // HCI server port
    command.AddParameter(instance_.rootcanal_hci_port());
    // Link server port
    command.AddParameter(instance_.rootcanal_link_port());
    // Bluetooth controller properties file
    command.AddParameter("--controller_properties_file=",
                         instance_.rootcanal_config_file());
    // Default commands file
    command.AddParameter("--default_commands_file=",
                         instance_.rootcanal_default_commands_file());

    return single_element_emplace(std::move(command));
  }

  // Feature
  bool Enabled() const override { return config_.enable_host_bluetooth(); }
  std::string Name() const override { return "RootCanal"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override { return true; }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
};

class LogcatReceiver : public CommandSource, public DiagnosticInformation {
 public:
  INJECT(LogcatReceiver(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}
  // DiagnosticInformation
  std::vector<std::string> Diagnostics() const override {
    return {"Logcat output: " + instance_.logcat_path()};
  }

  // CommandSource
  std::vector<Command> Commands() override {
    Command command(LogcatReceiverBinary());
    command.AddParameter("-log_pipe_fd=", pipe_);
    return single_element_emplace(std::move(command));
  }

  // Feature
  bool Enabled() const override { return true; }
  std::string Name() const override { return "LogcatReceiver"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override {
    auto log_name = instance_.logcat_pipe_name();
    if (mkfifo(log_name.c_str(), 0600) != 0) {
      LOG(ERROR) << "Unable to create named pipe at " << log_name << ": "
                 << strerror(errno);
      return false;
    }
    // Open the pipe here (from the launcher) to ensure the pipe is not deleted
    // due to the usage counters in the kernel reaching zero. If this is not
    // done and the logcat_receiver crashes for some reason the VMM may get
    // SIGPIPE.
    pipe_ = SharedFD::Open(log_name.c_str(), O_RDWR);
    if (!pipe_->IsOpen()) {
      LOG(ERROR) << "Can't open \"" << log_name << "\": " << pipe_->StrError();
      return false;
    }
    return true;
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD pipe_;
};

class ConfigServer : public CommandSource {
 public:
  INJECT(ConfigServer(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // CommandSource
  std::vector<Command> Commands() override {
    Command cmd(ConfigServerBinary());
    cmd.AddParameter("-server_fd=", socket_);
    return single_element_emplace(std::move(cmd));
  }

  // Feature
  bool Enabled() const override { return true; }
  std::string Name() const override { return "ConfigServer"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override {
    auto port = instance_.config_server_port();
    socket_ = SharedFD::VsockServer(port, SOCK_STREAM);
    if (!socket_->IsOpen()) {
      LOG(ERROR) << "Unable to create configuration server socket: "
                 << socket_->StrError();
      return false;
    }
    return true;
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD socket_;
};

class TombstoneReceiver : public CommandSource {
 public:
  INJECT(TombstoneReceiver(const CuttlefishConfig::InstanceSpecific& instance))
      : instance_(instance) {}

  // CommandSource
  std::vector<Command> Commands() override {
    Command cmd(TombstoneReceiverBinary());
    cmd.AddParameter("-server_fd=", socket_);
    cmd.AddParameter("-tombstone_dir=", tombstone_dir_);
    return single_element_emplace(std::move(cmd));
  }

  // Feature
  bool Enabled() const override { return true; }
  std::string Name() const override { return "TombstoneReceiver"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override {
    tombstone_dir_ = instance_.PerInstancePath("tombstones");
    if (!DirectoryExists(tombstone_dir_.c_str())) {
      LOG(DEBUG) << "Setting up " << tombstone_dir_;
      if (mkdir(tombstone_dir_.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) <
          0) {
        LOG(ERROR) << "Failed to create tombstone directory: " << tombstone_dir_
                   << ". Error: " << errno;
        return false;
      }
    }

    auto port = instance_.tombstone_receiver_port();
    socket_ = SharedFD::VsockServer(port, SOCK_STREAM);
    if (!socket_->IsOpen()) {
      LOG(ERROR) << "Unable to create tombstone server socket: "
                 << socket_->StrError();
      return false;
    }
    return true;
  }

 private:
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD socket_;
  std::string tombstone_dir_;
};

class MetricsService : public CommandSource {
 public:
  INJECT(MetricsService(const CuttlefishConfig& config)) : config_(config) {}

  // CommandSource
  std::vector<Command> Commands() override {
    return single_element_emplace(Command(MetricsBinary()));
  }

  // Feature
  bool Enabled() const override {
    return config_.enable_metrics() == CuttlefishConfig::kYes;
  }
  std::string Name() const override { return "MetricsService"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
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
    gnss_grpc_proxy_cmd.AddParameter("--gnss_grpc_port=",
                                     gnss_grpc_proxy_server_port);
    if (!instance_.gnss_file_path().empty()) {
      // If path is provided, proxy will start as local mode.
      gnss_grpc_proxy_cmd.AddParameter("--gnss_file_path=",
                                       instance_.gnss_file_path());
    }
    return single_element_emplace(std::move(gnss_grpc_proxy_cmd));
  }

  // Feature
  bool Enabled() const override {
    return config_.enable_gnss_grpc_proxy() &&
           FileExists(GnssGrpcProxyBinary());
  }

  std::string Name() const override { return "GnssGrpcProxyServer"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override {
    auto gnss_in_pipe_name = instance_.gnss_in_pipe_name();
    if (mkfifo(gnss_in_pipe_name.c_str(), 0600) != 0) {
      auto error = errno;
      LOG(ERROR) << "Failed to create gnss input fifo for crosvm: "
                 << strerror(error);
      return false;
    }

    auto gnss_out_pipe_name = instance_.gnss_out_pipe_name();
    if (mkfifo(gnss_out_pipe_name.c_str(), 0660) != 0) {
      auto error = errno;
      LOG(ERROR) << "Failed to create gnss output fifo for crosvm: "
                 << strerror(error);
      return false;
    }

    // These fds will only be read from or written to, but open them with
    // read and write access to keep them open in case the subprocesses exit
    gnss_grpc_proxy_in_wr_ = SharedFD::Open(gnss_in_pipe_name.c_str(), O_RDWR);
    if (!gnss_grpc_proxy_in_wr_->IsOpen()) {
      LOG(ERROR) << "Failed to open gnss_grpc_proxy input fifo for writes: "
                 << gnss_grpc_proxy_in_wr_->StrError();
      return false;
    }

    gnss_grpc_proxy_out_rd_ =
        SharedFD::Open(gnss_out_pipe_name.c_str(), O_RDWR);
    if (!gnss_grpc_proxy_out_rd_->IsOpen()) {
      LOG(ERROR) << "Failed to open gnss_grpc_proxy output fifo for reads: "
                 << gnss_grpc_proxy_out_rd_->StrError();
      return false;
    }
    return true;
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD gnss_grpc_proxy_in_wr_;
  SharedFD gnss_grpc_proxy_out_rd_;
};

class BluetoothConnector : public CommandSource {
 public:
  INJECT(BluetoothConnector(const CuttlefishConfig& config,
                            const CuttlefishConfig::InstanceSpecific& instance))
      : config_(config), instance_(instance) {}

  // CommandSource
  std::vector<Command> Commands() override {
    Command command(DefaultHostArtifactsPath("bin/bt_connector"));
    command.AddParameter("-bt_out=", fifos_[0]);
    command.AddParameter("-bt_in=", fifos_[1]);
    command.AddParameter("-hci_port=", instance_.rootcanal_hci_port());
    command.AddParameter("-link_port=", instance_.rootcanal_link_port());
    command.AddParameter("-test_port=", instance_.rootcanal_test_port());
    return single_element_emplace(std::move(command));
  }

  // Feature
  bool Enabled() const override { return config_.enable_host_bluetooth(); }

  std::string Name() const override { return "BluetoothConnector"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override {
    std::vector<std::string> fifo_paths = {
        instance_.PerInstanceInternalPath("bt_fifo_vm.in"),
        instance_.PerInstanceInternalPath("bt_fifo_vm.out"),
    };
    for (const auto& path : fifo_paths) {
      unlink(path.c_str());
      if (mkfifo(path.c_str(), 0660) < 0) {
        PLOG(ERROR) << "Could not create " << path;
        return false;
      }
      auto fd = SharedFD::Open(path, O_RDWR);
      if (!fd->IsOpen()) {
        LOG(ERROR) << "Could not open " << path << ": " << fd->StrError();
        return false;
      }
      fifos_.push_back(fd);
    }
    return true;
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  std::vector<SharedFD> fifos_;
};

class SecureEnvironment : public CommandSource {
 public:
  INJECT(SecureEnvironment(const CuttlefishConfig& config,
                           const CuttlefishConfig::InstanceSpecific& instance,
                           KernelLogPipeProvider& kernel_log_pipe_provider))
      : config_(config),
        instance_(instance),
        kernel_log_pipe_provider_(kernel_log_pipe_provider) {}

  // CommandSource
  std::vector<Command> Commands() override {
    Command command(HostBinaryPath("secure_env"));
    command.AddParameter("-keymaster_fd_out=", fifos_[0]);
    command.AddParameter("-keymaster_fd_in=", fifos_[1]);
    command.AddParameter("-gatekeeper_fd_out=", fifos_[2]);
    command.AddParameter("-gatekeeper_fd_in=", fifos_[3]);

    const auto& secure_hals = config_.secure_hals();
    bool secure_keymint = secure_hals.count(SecureHal::Keymint) > 0;
    command.AddParameter("-keymint_impl=", secure_keymint ? "tpm" : "software");
    bool secure_gatekeeper = secure_hals.count(SecureHal::Gatekeeper) > 0;
    auto gatekeeper_impl = secure_gatekeeper ? "tpm" : "software";
    command.AddParameter("-gatekeeper_impl=", gatekeeper_impl);

    command.AddParameter("-kernel_events_fd=", kernel_log_pipe_);

    return single_element_emplace(std::move(command));
  }

  // Feature
  bool Enabled() const override { return config_.enable_host_bluetooth(); }
  std::string Name() const override { return "SecureEnvironment"; }
  std::unordered_set<Feature*> Dependencies() const override {
    return {&kernel_log_pipe_provider_};
  }

 protected:
  bool Setup() override {
    std::vector<std::string> fifo_paths = {
        instance_.PerInstanceInternalPath("keymaster_fifo_vm.in"),
        instance_.PerInstanceInternalPath("keymaster_fifo_vm.out"),
        instance_.PerInstanceInternalPath("gatekeeper_fifo_vm.in"),
        instance_.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
    };
    std::vector<SharedFD> fifos;
    for (const auto& path : fifo_paths) {
      unlink(path.c_str());
      if (mkfifo(path.c_str(), 0600) < 0) {
        PLOG(ERROR) << "Could not create " << path;
        return false;
      }
      auto fd = SharedFD::Open(path, O_RDWR);
      if (!fd->IsOpen()) {
        LOG(ERROR) << "Could not open " << path << ": " << fd->StrError();
        return false;
      }
      fifos_.push_back(fd);
    }

    kernel_log_pipe_ = kernel_log_pipe_provider_.KernelLogPipe();

    return true;
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  std::vector<SharedFD> fifos_;
  KernelLogPipeProvider& kernel_log_pipe_provider_;
  SharedFD kernel_log_pipe_;
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

  // Feature
  bool Enabled() const override {
    return config_.enable_vehicle_hal_grpc_server() &&
           FileExists(VehicleHalGrpcServerBinary());
  }
  std::string Name() const override { return "VehicleHalServer"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
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

  // Feature
  bool Enabled() const override { return config_.console(); }
  std::string Name() const override { return "ConsoleForwarder"; }
  std::unordered_set<Feature*> Dependencies() const override { return {}; }

 protected:
  bool Setup() override {
    auto console_in_pipe_name = instance_.console_in_pipe_name();
    if (mkfifo(console_in_pipe_name.c_str(), 0600) != 0) {
      auto error = errno;
      LOG(ERROR) << "Failed to create console input fifo for crosvm: "
                 << strerror(error);
      return false;
    }

    auto console_out_pipe_name = instance_.console_out_pipe_name();
    if (mkfifo(console_out_pipe_name.c_str(), 0660) != 0) {
      auto error = errno;
      LOG(ERROR) << "Failed to create console output fifo for crosvm: "
                 << strerror(error);
      return false;
    }

    // These fds will only be read from or written to, but open them with
    // read and write access to keep them open in case the subprocesses exit
    console_forwarder_in_wr_ =
        SharedFD::Open(console_in_pipe_name.c_str(), O_RDWR);
    if (!console_forwarder_in_wr_->IsOpen()) {
      LOG(ERROR) << "Failed to open console_forwarder input fifo for writes: "
                 << console_forwarder_in_wr_->StrError();
      return false;
    }

    console_forwarder_out_rd_ =
        SharedFD::Open(console_out_pipe_name.c_str(), O_RDWR);
    if (!console_forwarder_out_rd_->IsOpen()) {
      LOG(ERROR) << "Failed to open console_forwarder output fifo for reads: "
                 << console_forwarder_out_rd_->StrError();
      return false;
    }
    return true;
  }

 private:
  const CuttlefishConfig& config_;
  const CuttlefishConfig::InstanceSpecific& instance_;
  SharedFD console_forwarder_in_wr_;
  SharedFD console_forwarder_out_rd_;
};

using PublicDeps = fruit::Required<const CuttlefishConfig,
                                   const CuttlefishConfig::InstanceSpecific>;
fruit::Component<PublicDeps, KernelLogPipeProvider> launchComponent() {
  using InternalDeps = fruit::Required<const CuttlefishConfig,
                                       const CuttlefishConfig::InstanceSpecific,
                                       KernelLogPipeProvider>;
  using Multi = Multibindings<InternalDeps>;
  using Bases = Multi::Bases<CommandSource, DiagnosticInformation, Feature>;
  return fruit::createComponent()
      .bind<KernelLogPipeProvider, KernelLogMonitor>()
      .install(Bases::Impls<BluetoothConnector>)
      .install(Bases::Impls<ConfigServer>)
      .install(Bases::Impls<ConsoleForwarder>)
      .install(Bases::Impls<GnssGrpcProxyServer>)
      .install(Bases::Impls<KernelLogMonitor>)
      .install(Bases::Impls<LogcatReceiver>)
      .install(Bases::Impls<MetricsService>)
      .install(Bases::Impls<RootCanal>)
      .install(Bases::Impls<SecureEnvironment>)
      .install(Bases::Impls<TombstoneReceiver>)
      .install(Bases::Impls<VehicleHalServer>);
}

} // namespace cuttlefish
