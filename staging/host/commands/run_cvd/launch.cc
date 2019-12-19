#include "host/commands/run_cvd/launch.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/size_utils.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/commands/run_cvd/pre_launch_initializers.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

using cvd::RunnerExitCodes;
using cvd::MonitorEntry;

namespace {

std::string GetAdbConnectorTcpArg(const vsoc::CuttlefishConfig& config) {
  return std::string{"127.0.0.1:"} + std::to_string(config.host_port());
}

std::string GetAdbConnectorVsockArg(const vsoc::CuttlefishConfig& config) {
  return std::string{"vsock:"}
      + std::to_string(config.vsock_guest_cid())
      + std::string{":5555"};
}

bool AdbModeEnabled(const vsoc::CuttlefishConfig& config, vsoc::AdbMode mode) {
  return config.adb_mode().count(mode) > 0;
}

bool AdbVsockTunnelEnabled(const vsoc::CuttlefishConfig& config) {
  return config.vsock_guest_cid() > 2
      && AdbModeEnabled(config, vsoc::AdbMode::VsockTunnel);
}

bool AdbVsockHalfTunnelEnabled(const vsoc::CuttlefishConfig& config) {
  return config.vsock_guest_cid() > 2
      && AdbModeEnabled(config, vsoc::AdbMode::VsockHalfTunnel);
}

bool AdbTcpConnectorEnabled(const vsoc::CuttlefishConfig& config) {
  bool vsock_tunnel = AdbVsockTunnelEnabled(config);
  bool vsock_half_tunnel = AdbVsockHalfTunnelEnabled(config);
  return config.run_adb_connector() && (vsock_tunnel || vsock_half_tunnel);
}

bool AdbVsockConnectorEnabled(const vsoc::CuttlefishConfig& config) {
  return config.run_adb_connector()
      && AdbModeEnabled(config, vsoc::AdbMode::NativeVsock);
}

cvd::OnSocketReadyCb GetOnSubprocessExitCallback(
    const vsoc::CuttlefishConfig& config) {
  if (config.restart_subprocesses()) {
    return cvd::ProcessMonitor::RestartOnExitCb;
  } else {
    return cvd::ProcessMonitor::DoNotMonitorCb;
  }
}
} // namespace

bool LogcatReceiverEnabled(const vsoc::CuttlefishConfig& config) {
  return config.logcat_mode() == cvd::kLogcatVsockMode;
}

std::vector<cvd::SharedFD> LaunchKernelLogMonitor(
    const vsoc::CuttlefishConfig& config,
    cvd::ProcessMonitor* process_monitor,
    unsigned int number_of_event_pipes) {
  auto log_name = config.kernel_log_pipe_name();
  if (mkfifo(log_name.c_str(), 0600) != 0) {
    LOG(ERROR) << "Unable to create named pipe at " << log_name << ": "
               << strerror(errno);
    return {};
  }

  cvd::SharedFD pipe;
  // Open the pipe here (from the launcher) to ensure the pipe is not deleted
  // due to the usage counters in the kernel reaching zero. If this is not done
  // and the kernel_log_monitor crashes for some reason the VMM may get SIGPIPE.
  pipe = cvd::SharedFD::Open(log_name.c_str(), O_RDWR);
  cvd::Command command(config.kernel_log_monitor_binary());
  command.AddParameter("-log_pipe_fd=", pipe);

  std::vector<cvd::SharedFD> ret;

  if (number_of_event_pipes > 0) {
    auto param_builder = command.GetParameterBuilder();
    param_builder << "-subscriber_fds=";
    for (unsigned int i = 0; i < number_of_event_pipes; ++i) {
      cvd::SharedFD event_pipe_write_end, event_pipe_read_end;
      if (!cvd::SharedFD::Pipe(&event_pipe_read_end, &event_pipe_write_end)) {
        LOG(ERROR) << "Unable to create boot events pipe: " << strerror(errno);
        std::exit(RunnerExitCodes::kPipeIOError);
      }
      if (i > 0) {
        param_builder << ",";
      }
      param_builder << event_pipe_write_end;
      ret.push_back(event_pipe_read_end);
    }
    param_builder.Build();
  }

  process_monitor->StartSubprocess(std::move(command),
                                   GetOnSubprocessExitCallback(config));

  return ret;
}

LogcatServerPorts LaunchLogcatReceiverIfEnabled(const vsoc::CuttlefishConfig& config,
                                                cvd::ProcessMonitor* process_monitor) {
  if (!LogcatReceiverEnabled(config)) {
    return {};
  }
  auto socket = cvd::SharedFD::VsockServer(SOCK_STREAM);
  if (!socket->IsOpen()) {
    LOG(ERROR) << "Unable to create logcat server socket: "
               << socket->StrError();
    std::exit(RunnerExitCodes::kLogcatServerError);
  }
  cvd::Command cmd(config.logcat_receiver_binary());
  cmd.AddParameter("-server_fd=", socket);
  process_monitor->StartSubprocess(std::move(cmd),
                                   GetOnSubprocessExitCallback(config));
  return { socket->VsockServerPort() };
}

ConfigServerPorts LaunchConfigServer(const vsoc::CuttlefishConfig& config,
                                     cvd::ProcessMonitor* process_monitor) {
  auto socket = cvd::SharedFD::VsockServer(SOCK_STREAM);
  if (!socket->IsOpen()) {
    LOG(ERROR) << "Unable to create configuration server socket: "
               << socket->StrError();
    std::exit(RunnerExitCodes::kConfigServerError);
  }
  cvd::Command cmd(config.config_server_binary());
  cmd.AddParameter("-server_fd=", socket);
  process_monitor->StartSubprocess(std::move(cmd),
                                   GetOnSubprocessExitCallback(config));
  return { socket->VsockServerPort() };
}

TombstoneReceiverPorts LaunchTombstoneReceiverIfEnabled(
    const vsoc::CuttlefishConfig& config, cvd::ProcessMonitor* process_monitor) {
  if (!config.enable_tombstone_receiver()) {
    return {};
  }

  std::string tombstoneDir = config.PerInstancePath("tombstones");
  if (!cvd::DirectoryExists(tombstoneDir.c_str())) {
    LOG(INFO) << "Setting up " << tombstoneDir;
    if (mkdir(tombstoneDir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) <
        0) {
      LOG(ERROR) << "Failed to create tombstone directory: " << tombstoneDir
                 << ". Error: " << errno;
      exit(RunnerExitCodes::kTombstoneDirCreationError);
      return {};
    }
  }

  auto socket = cvd::SharedFD::VsockServer(SOCK_STREAM);
  if (!socket->IsOpen()) {
    LOG(ERROR) << "Unable to create tombstone server socket: "
               << socket->StrError();
    std::exit(RunnerExitCodes::kTombstoneServerError);
    return {};
  }
  cvd::Command cmd(config.tombstone_receiver_binary());
  cmd.AddParameter("-server_fd=", socket);
  cmd.AddParameter("-tombstone_dir=", tombstoneDir);

  process_monitor->StartSubprocess(std::move(cmd),
                                   GetOnSubprocessExitCallback(config));
  return { socket->VsockServerPort() };
}

cvd::SharedFD CreateUnixVncInputServer(const std::string& path) {
  auto server = cvd::SharedFD::SocketLocalServer(path.c_str(), false, SOCK_STREAM, 0666);
  if (!server->IsOpen()) {
    LOG(ERROR) << "Unable to create unix input server: "
               << server->StrError();
    return cvd::SharedFD();
  }
  return server;
}

VncServerPorts LaunchVNCServerIfEnabled(
    const vsoc::CuttlefishConfig& config,
    cvd::ProcessMonitor* process_monitor,
    std::function<bool(MonitorEntry*)> callback) {
  VncServerPorts server_ret;
  if (!config.enable_vnc_server()) {
    return {};
  }
  std::set<std::string> extra_kernel_args;
  // Launch the vnc server, don't wait for it to complete
  auto port_options = "-port=" + std::to_string(config.vnc_server_port());
  cvd::Command vnc_server(config.vnc_server_binary());
  vnc_server.AddParameter(port_options);
  if (config.vm_manager() == vm_manager::QemuManager::name()) {
    vnc_server.AddParameter("-write_virtio_input");
  }
  // When the ivserver is not enabled, the vnc touch_server needs to serve
  // on sockets and send input events to whoever connects to it (the VMM).
  cvd::SharedFD touch_server;
  if (config.vm_manager() == vm_manager::CrosvmManager::name()) {
    touch_server = CreateUnixVncInputServer(config.touch_socket_path());
  } else {
    touch_server = cvd::SharedFD::VsockServer(SOCK_STREAM);
    server_ret.touch_server_vsock_port = touch_server->VsockServerPort();
  }
  if (!touch_server->IsOpen()) {
    LOG(ERROR) << "Could not open touch server: " << touch_server->StrError();
    return {};
  }
  vnc_server.AddParameter("-touch_fd=", touch_server);

  cvd::SharedFD keyboard_server;
  if (config.vm_manager() == vm_manager::CrosvmManager::name()) {
    keyboard_server = CreateUnixVncInputServer(config.keyboard_socket_path());
  } else {
    keyboard_server = cvd::SharedFD::VsockServer(SOCK_STREAM);
    server_ret.keyboard_server_vsock_port = keyboard_server->VsockServerPort();
  }
  if (!keyboard_server->IsOpen()) {
    LOG(ERROR) << "Could not open keyboard server: " << keyboard_server->StrError();
    return {};
  }
  vnc_server.AddParameter("-keyboard_fd=", keyboard_server);
  // TODO(b/128852363): This should be handled through the wayland mock
  //  instead.
  // Additionally it receives the frame updates from a virtual socket
  // instead
  auto frames_server = cvd::SharedFD::VsockServer(SOCK_STREAM);
  server_ret.frames_server_vsock_port = frames_server->VsockServerPort();
  if (!frames_server->IsOpen()) {
    LOG(ERROR) << "Could not open frames server: " << frames_server->StrError();
    return {};
  }
  vnc_server.AddParameter("-frame_server_fd=", frames_server);
  process_monitor->StartSubprocess(std::move(vnc_server), callback);

  return server_ret;
}

void LaunchAdbConnectorIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config,
                                 cvd::SharedFD adbd_events_pipe) {
  cvd::Command adb_connector(config.adb_connector_binary());
  adb_connector.AddParameter("-adbd_events_fd=", adbd_events_pipe);
  std::set<std::string> addresses;

  if (AdbTcpConnectorEnabled(config)) {
    addresses.insert(GetAdbConnectorTcpArg(config));
  }
  if (AdbVsockConnectorEnabled(config)) {
    addresses.insert(GetAdbConnectorVsockArg(config));
  }

  if (addresses.size() > 0) {
    std::string address_arg = "--addresses=";
    for (auto& arg : addresses) {
      address_arg += arg + ",";
    }
    address_arg.pop_back();
    adb_connector.AddParameter(address_arg);
    process_monitor->StartSubprocess(std::move(adb_connector),
                                     GetOnSubprocessExitCallback(config));
  }
}

void LaunchSocketVsockProxyIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config) {
  if (AdbVsockTunnelEnabled(config)) {
    cvd::Command adb_tunnel(config.socket_vsock_proxy_binary());
    adb_tunnel.AddParameter("--vsock_port=6520");
    adb_tunnel.AddParameter(
        std::string{"--tcp_port="} + std::to_string(config.host_port()));
    adb_tunnel.AddParameter(std::string{"--vsock_guest_cid="} +
                            std::to_string(config.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
  if (AdbVsockHalfTunnelEnabled(config)) {
    cvd::Command adb_tunnel(config.socket_vsock_proxy_binary());
    adb_tunnel.AddParameter("--vsock_port=5555");
    adb_tunnel.AddParameter(
        std::string{"--tcp_port="} + std::to_string(config.host_port()));
    adb_tunnel.AddParameter(std::string{"--vsock_guest_cid="} +
                            std::to_string(config.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
}
