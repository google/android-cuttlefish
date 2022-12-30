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

struct GuestConfig {
  Arch target_arch;
  bool bootconfig_supported;
  bool hctr2_supported;
  std::string android_version_number;
};

Result<std::vector<GuestConfig>> GetGuestConfigAndSetDefaults();
// Must be called after ParseCommandLineFlags.
Result<CuttlefishConfig> InitializeCuttlefishConfiguration(
    const std::string& root_dir,
    const std::vector<GuestConfig>& guest_configs,
    fruit::Injector<>& injector, const FetcherConfig& fetcher_config);

std::string GetConfigFilePath(const CuttlefishConfig& config);
std::string GetCuttlefishEnvPath();
std::string GetSeccompPolicyDir();

} // namespace cuttlefish
