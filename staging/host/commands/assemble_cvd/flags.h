#pragma once

#include <cstdint>
#include <optional>

#include "common/libs/utils/environment.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

namespace cuttlefish {

struct KernelConfig {
  Arch target_arch;
  bool bootconfig_supported;
};

bool ParseCommandLineFlags(int* argc, char*** argv,
                           KernelConfig* kernel_config);
// Must be called after ParseCommandLineFlags.
CuttlefishConfig InitializeCuttlefishConfiguration(
    const std::string& instance_dir, int modem_simulator_count,
    KernelConfig kernel_config);

std::string GetConfigFilePath(const CuttlefishConfig& config);
std::string GetCuttlefishEnvPath();

} // namespace cuttlefish
