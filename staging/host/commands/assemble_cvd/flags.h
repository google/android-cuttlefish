#pragma once

#include <cstdint>
#include <optional>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

namespace cuttlefish {

const CuttlefishConfig* InitFilesystemAndCreateConfig(
    int* argc, char*** argv, FetcherConfig config);
std::string GetConfigFilePath(const CuttlefishConfig& config);
std::string GetCuttlefishEnvPath();

} // namespace cuttlefish
