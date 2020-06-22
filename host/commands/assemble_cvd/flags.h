#pragma once

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

const vsoc::CuttlefishConfig* InitFilesystemAndCreateConfig(
    int* argc, char*** argv, cuttlefish::FetcherConfig config);
std::string GetConfigFilePath(const vsoc::CuttlefishConfig& config);
