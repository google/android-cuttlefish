#pragma once

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"

cvd::Command GetIvServerCommand(const vsoc::CuttlefishConfig& config);
cvd::Command GetKernelLogMonitorCommand(const vsoc::CuttlefishConfig& config,
                                        cvd::SharedFD* boot_events_pipe);
