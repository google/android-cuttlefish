#pragma once

#include <cstdint>
#include <optional>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

const cuttlefish::CuttlefishConfig* InitFilesystemAndCreateConfig(
    int* argc, char*** argv, cuttlefish::FetcherConfig config);
std::string GetConfigFilePath(const cuttlefish::CuttlefishConfig& config);
std::string GetCuttlefishEnvPath();
