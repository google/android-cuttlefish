#pragma once

#include <fruit/fruit.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/libs/utils/environment.h"
#include "common/libs/utils/result.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

namespace cuttlefish {

struct KernelConfig {
  Arch target_arch;
  bool bootconfig_supported;
};

Result<std::vector<KernelConfig>> GetKernelConfigAndSetDefaults();
// Must be called after ParseCommandLineFlags.
CuttlefishConfig InitializeCuttlefishConfiguration(const std::string& root_dir,
                                                   int modem_simulator_count,
                                                   const std::vector<KernelConfig>& kernel_configs,
                                                   fruit::Injector<>& injector,
                                                   const FetcherConfig& fetcher_config);

std::string GetConfigFilePath(const CuttlefishConfig& config);
std::string GetCuttlefishEnvPath();
std::string GetSeccompPolicyDir();

} // namespace cuttlefish
