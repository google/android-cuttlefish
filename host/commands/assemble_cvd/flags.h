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
    const BootImageUnpacker& boot_image_unpacker,
    const FetcherConfig& fetcher_config);

std::string GetConfigFilePath(const CuttlefishConfig& config);
std::string GetCuttlefishEnvPath();

} // namespace cuttlefish
