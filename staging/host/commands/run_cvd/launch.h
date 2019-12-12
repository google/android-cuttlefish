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
void LaunchLogcatReceiverIfEnabled(const vsoc::CuttlefishConfig& config,
                                   cvd::ProcessMonitor* process_monitor);
void LaunchConfigServer(const vsoc::CuttlefishConfig& config,
                        cvd::ProcessMonitor* process_monitor);
void LaunchUsbServerIfEnabled(const vsoc::CuttlefishConfig& config,
                              cvd::ProcessMonitor* process_monitor);
void LaunchAdbConnectorIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config,
                                 cvd::SharedFD adbd_events_pipe);
void LaunchSocketVsockProxyIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config);
void LaunchTombstoneReceiverIfEnabled(const vsoc::CuttlefishConfig& config,
                                      cvd::ProcessMonitor* process_monitor);

struct VncServerPorts {
  std::optional<int> frames_server_vsock_port;
  std::optional<int> touch_server_vsock_port;
  std::optional<int> keyboard_server_vsock_port;
};
VncServerPorts LaunchVNCServerIfEnabled(
    const vsoc::CuttlefishConfig& config,
    cvd::ProcessMonitor* process_monitor,
    std::function<bool(cvd::MonitorEntry*)> callback);
