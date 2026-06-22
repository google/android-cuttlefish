/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include "cuttlefish/host/commands/cvd/cli/parser/instance/cf_connectivity_configs.h"

#include <algorithm>
#include <string>
#include <vector>

#include "cuttlefish/host/commands/assemble_cvd/flags_defaults.h"
#include "cuttlefish/host/commands/cvd/cli/parser/cf_configs_common.h"
#include "cuttlefish/host/commands/cvd/cli/parser/load_config.pb.h"

namespace cuttlefish {

inline constexpr char kFlagModemSimulatorSimType[] = "modem_simulator_sim_type";

using cvd::config::EnvironmentSpecification;
using cvd::config::Instance;

static std::string VsockGuestGroup(const Instance& instance) {
  return instance.connectivity().vsock().guest_group();
}

static int32_t ModemSimulatorSimType(const Instance& instance) {
  switch (instance.connectivity().modem_simulator_sim_type()) {
    case cvd::config::MODEM_SIMULATOR_SIM_TYPE_NORMAL:
      return 1;
    case cvd::config::MODEM_SIMULATOR_SIM_TYPE_CTS_CARRIER_API:
      return 2;
    case cvd::config::MODEM_SIMULATOR_SIM_TYPE_UNSPECIFIED:
    default:
      return CF_DEFAULTS_MODEM_SIMULATOR_SIM_TYPE;
  }
}

std::vector<std::string> GenerateConnectivityFlags(
    const EnvironmentSpecification& cfg) {
  std::vector<std::string> flags = {
      GenerateInstanceFlag("vsock_guest_group", cfg, VsockGuestGroup),
  };
  const bool has_sim_type = std::any_of(
      cfg.instances().begin(), cfg.instances().end(), [](const Instance& ins) {
        return ins.connectivity().has_modem_simulator_sim_type();
      });
  if (has_sim_type) {
    flags.push_back(GenerateInstanceFlag(kFlagModemSimulatorSimType, cfg,
                                         ModemSimulatorSimType));
  }
  return flags;
}

}  // namespace cuttlefish
