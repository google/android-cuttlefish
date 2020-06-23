#pragma once

#include <functional>
#include <set>
#include <string>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/libs/config/cuttlefish_config.h"

std::vector <cuttlefish::SharedFD> LaunchKernelLogMonitor(
    const cuttlefish::CuttlefishConfig& config,
    cuttlefish::ProcessMonitor* process_monitor,
    unsigned int number_of_event_pipes);
void LaunchAdbConnectorIfEnabled(cuttlefish::ProcessMonitor* process_monitor,
                                 const cuttlefish::CuttlefishConfig& config,
                                 cuttlefish::SharedFD adbd_events_pipe);
void LaunchSocketVsockProxyIfEnabled(cuttlefish::ProcessMonitor* process_monitor,
                                 const cuttlefish::CuttlefishConfig& config);

struct StreamerLaunchResult {
  bool launched = false;
};
StreamerLaunchResult LaunchVNCServer(
    const cuttlefish::CuttlefishConfig& config,
    cuttlefish::ProcessMonitor* process_monitor,
    std::function<bool(cuttlefish::MonitorEntry*)> callback);

void LaunchTombstoneReceiverIfEnabled(const cuttlefish::CuttlefishConfig& config,
                                      cuttlefish::ProcessMonitor* process_monitor);
void LaunchLogcatReceiverIfEnabled(const cuttlefish::CuttlefishConfig& config,
                                   cuttlefish::ProcessMonitor* process_monitor);
void LaunchConfigServer(const cuttlefish::CuttlefishConfig& config,
                        cuttlefish::ProcessMonitor* process_monitor);

StreamerLaunchResult LaunchWebRTC(cuttlefish::ProcessMonitor* process_monitor,
                                  const cuttlefish::CuttlefishConfig& config);

void LaunchTpm(cuttlefish::ProcessMonitor* process_monitor,
               const cuttlefish::CuttlefishConfig& config);

void LaunchMetrics(cuttlefish::ProcessMonitor* process_monitor,
                                  const cuttlefish::CuttlefishConfig& config);

void LaunchSecureEnvironment(cuttlefish::ProcessMonitor* process_monitor,
                             const cuttlefish::CuttlefishConfig& config);
