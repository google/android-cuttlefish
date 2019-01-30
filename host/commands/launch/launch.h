#pragma once

#include <functional>

#include "common/libs/utils/subprocess.h"
#include "host/commands/launch/process_monitor.h"
#include "host/libs/config/cuttlefish_config.h"

cvd::Command GetIvServerCommand(const vsoc::CuttlefishConfig& config);
cvd::Command GetKernelLogMonitorCommand(const vsoc::CuttlefishConfig& config,
                                        cvd::SharedFD* boot_events_pipe);
void LaunchVNCServerIfEnabled(const vsoc::CuttlefishConfig& config,
                              cvd::ProcessMonitor* process_monitor,
                              std::function<bool(cvd::MonitorEntry*)> callback);
void LaunchStreamAudioIfEnabled(const vsoc::CuttlefishConfig& config,
                                cvd::ProcessMonitor* process_monitor,
                                std::function<bool(cvd::MonitorEntry*)> callback);
