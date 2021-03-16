#pragma once

#include <cstdint>
#include <optional>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

namespace cuttlefish {

bool ParseCommandLineFlags(int* argc, char*** argv);
// Must be called after ParseCommandLineFlags.
CuttlefishConfig InitializeCuttlefishConfiguration(
    const std::string& assembly_dir, const std::string& instance_dir,
    int modem_simulator_count);

std::string GetConfigFilePath(const CuttlefishConfig& config);
std::string GetCuttlefishEnvPath();

} // namespace cuttlefish
