#pragma once

#include <functional>
#include <set>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/libs/config/cuttlefish_config.h"

std::vector <cvd::SharedFD> LaunchKernelLogMonitor(
    const vsoc::CuttlefishConfig& config,
    cvd::ProcessMonitor* process_monitor,
    unsigned int number_of_event_pipes);
void LaunchAdbConnectorIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config,
                                 cvd::SharedFD adbd_events_pipe);
void LaunchSocketVsockProxyIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config);

struct StreamerLaunchResult {
  bool launched = false;
};
StreamerLaunchResult LaunchVNCServer(
    const vsoc::CuttlefishConfig& config,
    cvd::ProcessMonitor* process_monitor,
    std::function<bool(cvd::MonitorEntry*)> callback);

void LaunchTombstoneReceiverIfEnabled(const vsoc::CuttlefishConfig& config,
                                      cvd::ProcessMonitor* process_monitor);
void LaunchLogcatReceiverIfEnabled(const vsoc::CuttlefishConfig& config,
                                   cvd::ProcessMonitor* process_monitor);
void LaunchConfigServer(const vsoc::CuttlefishConfig& config,
                        cvd::ProcessMonitor* process_monitor);

StreamerLaunchResult LaunchWebRTC(cvd::ProcessMonitor* process_monitor,
                                  const vsoc::CuttlefishConfig& config);

void LaunchTpm(cvd::ProcessMonitor* process_monitor,
               const vsoc::CuttlefishConfig& config);

void LaunchMetrics(cvd::ProcessMonitor* process_monitor,
                                  const vsoc::CuttlefishConfig& config);
