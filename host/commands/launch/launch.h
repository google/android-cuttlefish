#pragma once

#include <functional>

#include "common/libs/utils/subprocess.h"
#include "host/commands/launch/process_monitor.h"
#include "host/libs/config/cuttlefish_config.h"

int GetHostPort();
bool AdbUsbEnabled(const vsoc::CuttlefishConfig& config);
void ValidateAdbModeFlag(const vsoc::CuttlefishConfig& config);

cvd::Command GetIvServerCommand(const vsoc::CuttlefishConfig& config);
std::vector <cvd::SharedFD> LaunchKernelLogMonitor(
    const vsoc::CuttlefishConfig& config,
    cvd::ProcessMonitor* process_monitor,
    unsigned int number_of_event_pipes);
void LaunchLogcatReceiverIfEnabled(const vsoc::CuttlefishConfig& config,
                                   cvd::ProcessMonitor* process_monitor);
void LaunchConfigServer(const vsoc::CuttlefishConfig& config,
                        cvd::ProcessMonitor* process_monitor);
void LaunchUsbServerIfEnabled(const vsoc::CuttlefishConfig& config,
                              cvd::ProcessMonitor* process_monitor);
bool LaunchVNCServerIfEnabled(const vsoc::CuttlefishConfig& config,
                              cvd::ProcessMonitor* process_monitor,
                              std::function<bool(cvd::MonitorEntry*)> callback);
void LaunchStreamAudioIfEnabled(const vsoc::CuttlefishConfig& config,
                                cvd::ProcessMonitor* process_monitor,
                                std::function<bool(cvd::MonitorEntry*)> callback);
void LaunchAdbConnectorIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config,
                                 cvd::SharedFD adbd_events_pipe);
void LaunchSocketForwardProxyIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config);
void LaunchSocketVsockProxyIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config);
void LaunchIvServerIfEnabled(cvd::ProcessMonitor* process_monitor,
                             const vsoc::CuttlefishConfig& config);
void LaunchTombstoneReceiverIfEnabled(const vsoc::CuttlefishConfig& config,
                                      cvd::ProcessMonitor* process_monitor);