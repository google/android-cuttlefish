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
                                 const cuttlefish::CuttlefishConfig& config);
void LaunchSocketVsockProxyIfEnabled(cuttlefish::ProcessMonitor* process_monitor,
                                     const cuttlefish::CuttlefishConfig& config,
                                     cuttlefish::SharedFD adbd_events_pipe);
void LaunchModemSimulatorIfEnabled(const cuttlefish::CuttlefishConfig& config,
                                   cuttlefish::ProcessMonitor* process_monitor);

void LaunchVNCServer(
    const cuttlefish::CuttlefishConfig& config,
    cuttlefish::ProcessMonitor* process_monitor,
    std::function<bool(cuttlefish::MonitorEntry*)> callback);

void LaunchTombstoneReceiver(const cuttlefish::CuttlefishConfig& config,
                             cuttlefish::ProcessMonitor* process_monitor);
void LaunchLogcatReceiver(const cuttlefish::CuttlefishConfig& config,
                          cuttlefish::ProcessMonitor* process_monitor);
void LaunchConfigServer(const cuttlefish::CuttlefishConfig& config,
                        cuttlefish::ProcessMonitor* process_monitor);

void LaunchWebRTC(cuttlefish::ProcessMonitor* process_monitor,
                  const cuttlefish::CuttlefishConfig& config,
                  cuttlefish::SharedFD kernel_log_events_pipe);

void LaunchMetrics(cuttlefish::ProcessMonitor* process_monitor,
                                  const cuttlefish::CuttlefishConfig& config);

void LaunchGnssGrpcProxyServerIfEnabled(const cuttlefish::CuttlefishConfig& config,
                                      cuttlefish::ProcessMonitor* process_monitor);

void LaunchSecureEnvironment(cuttlefish::ProcessMonitor* process_monitor,
                             const cuttlefish::CuttlefishConfig& config);

void LaunchVerhicleHalServerIfEnabled(const cuttlefish::CuttlefishConfig& config,
                                      cuttlefish::ProcessMonitor* process_monitor);

void LaunchConsoleForwarderIfEnabled(const cuttlefish::CuttlefishConfig& config,
                                     cuttlefish::ProcessMonitor* process_monitor);
