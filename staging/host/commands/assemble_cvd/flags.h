#pragma once

#include <cstdint>
#include <optional>

#include "host/commands/assemble_cvd/boot_image_unpacker.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

namespace cuttlefish {

bool ParseCommandLineFlags(int* argc, char*** argv);
// Must be called after ParseCommandLineFlags.
CuttlefishConfig InitializeCuttlefishConfiguration(
    const std::string& assembly_dir,
    const std::string& instance_dir,
    int modem_simulator_count,
    const BootImageUnpacker& boot_image_unpacker,
    const FetcherConfig& fetcher_config);

std::string GetConfigFilePath(const CuttlefishConfig& config);
std::string GetCuttlefishEnvPath();

} // namespace cuttlefish
