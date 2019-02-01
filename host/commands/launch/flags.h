#pragma once

#include "host/libs/config/cuttlefish_config.h"

vsoc::CuttlefishConfig* InitFilesystemAndCreateConfig(int* argc, char*** argv);
std::string GetConfigFilePath(const vsoc::CuttlefishConfig& config);
