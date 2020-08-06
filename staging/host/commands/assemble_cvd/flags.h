#pragma once

#include <cstdint>
#include <optional>

#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/fetcher_config.h"

const cuttlefish::CuttlefishConfig* InitFilesystemAndCreateConfig(
    int* argc, char*** argv, cuttlefish::FetcherConfig config);
std::string GetConfigFilePath(const cuttlefish::CuttlefishConfig& config);

struct IfaceData {
  std::string name;
  uint32_t session_id;
  uint32_t resource_id;
};

struct IfaceConfig {
  IfaceData mobile_tap;
  IfaceData wireless_tap;
};

// Acquires interfaces from the resource allocator daemon if it is enabled, 
// or fallse back to using the static resources created by the debian package
std::optional<IfaceConfig> AcquireIfaces(int num);
std::optional<IfaceConfig> RequestIfaces();
