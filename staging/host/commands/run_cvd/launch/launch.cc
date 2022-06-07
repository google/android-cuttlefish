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
      .install(GnssGrpcProxyServerComponent)
      .install(LogcatReceiverComponent)
      .install(KernelLogMonitorComponent)
      .install(OpenWrtComponent)
      .install(RootCanalComponent)
      .install(SecureEnvComponent)
      .install(TombstoneReceiverComponent)
      .install(VehicleHalServerComponent)
      .install(WmediumdServerComponent)
      .install(Bases::Impls<MetricsService>);
}

}  // namespace cuttlefish
