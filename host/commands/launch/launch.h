#pragma once

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

cvd::Command GetIvServerCommand(const vsoc::CuttlefishConfig& config);
