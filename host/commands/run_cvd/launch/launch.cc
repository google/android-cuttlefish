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
#include "host/commands/run_cvd/launch/log_tee_creator.h"
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

class MetricsService : public CommandSource {
 public:
  INJECT(MetricsService(const CuttlefishConfig& config)) : config_(config) {}

  // CommandSource
  Result<std::vector<Command>> Commands() override {
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
  Result<std::vector<Command>> Commands() override {
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
      .install(ConsoleForwarderComponent)
      .install(LogcatReceiverComponent)
      .install(KernelLogMonitorComponent)
      .install(OpenWrtComponent)
      .install(RootCanalComponent)
      .install(SecureEnvComponent)
      .install(TombstoneReceiverComponent)
      .install(VehicleHalServerComponent)
      .install(WmediumdServerComponent)
      .install(Bases::Impls<GnssGrpcProxyServer>)
      .install(Bases::Impls<MetricsService>);
}

}  // namespace cuttlefish
