#include "host/commands/run_cvd/launch.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <android-base/logging.h>

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
  auto instance = config.ForDefaultInstance();
  return std::string{"127.0.0.1:"} + std::to_string(instance.host_port());
}

std::string GetAdbConnectorVsockArg(const vsoc::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return std::string{"vsock:"}
      + std::to_string(instance.vsock_guest_cid())
      + std::string{":5555"};
}

bool AdbModeEnabled(const vsoc::CuttlefishConfig& config, vsoc::AdbMode mode) {
  return config.adb_mode().count(mode) > 0;
}

bool AdbVsockTunnelEnabled(const vsoc::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.vsock_guest_cid() > 2
      && AdbModeEnabled(config, vsoc::AdbMode::VsockTunnel);
}

bool AdbVsockHalfTunnelEnabled(const vsoc::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.vsock_guest_cid() > 2
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

cvd::SharedFD CreateUnixInputServer(const std::string& path) {
  auto server = cvd::SharedFD::SocketLocalServer(path.c_str(), false, SOCK_STREAM, 0666);
  if (!server->IsOpen()) {
    LOG(ERROR) << "Unable to create unix input server: "
               << server->StrError();
    return cvd::SharedFD();
  }
  return server;
}

// Creates the frame and input sockets and add the relevant arguments to the vnc
// server and webrtc commands
StreamerLaunchResult CreateStreamerServers(cvd::Command* cmd,
                                           const vsoc::CuttlefishConfig& config) {
  StreamerLaunchResult server_ret;
  cvd::SharedFD touch_server;
  cvd::SharedFD keyboard_server;

  auto instance = config.ForDefaultInstance();
  if (config.vm_manager() == vm_manager::QemuManager::name()) {
    cmd->AddParameter("-write_virtio_input");

    touch_server = cvd::SharedFD::VsockServer(SOCK_STREAM);
    server_ret.touch_server_vsock_port = touch_server->VsockServerPort();

    keyboard_server = cvd::SharedFD::VsockServer(SOCK_STREAM);
    server_ret.keyboard_server_vsock_port = keyboard_server->VsockServerPort();
  } else {
    touch_server = CreateUnixInputServer(instance.touch_socket_path());
    keyboard_server = CreateUnixInputServer(instance.keyboard_socket_path());
  }
  if (!touch_server->IsOpen()) {
    LOG(ERROR) << "Could not open touch server: " << touch_server->StrError();
    return {};
  }
  cmd->AddParameter("-touch_fd=", touch_server);

  if (!keyboard_server->IsOpen()) {
    LOG(ERROR) << "Could not open keyboard server: " << keyboard_server->StrError();
    return {};
  }
  cmd->AddParameter("-keyboard_fd=", keyboard_server);

  cvd::SharedFD frames_server;
  if (config.gpu_mode() == vsoc::kGpuModeDrmVirgl ||
      config.gpu_mode() == vsoc::kGpuModeGfxStream) {
    frames_server = CreateUnixInputServer(instance.frames_socket_path());
  } else {
    frames_server = cvd::SharedFD::VsockServer(SOCK_STREAM);
    server_ret.frames_server_vsock_port = frames_server->VsockServerPort();
  }
  if (!frames_server->IsOpen()) {
    LOG(ERROR) << "Could not open frames server: " << frames_server->StrError();
    return {};
  }
  cmd->AddParameter("-frame_server_fd=", frames_server);
  return server_ret;
}

} // namespace

bool LogcatReceiverEnabled(const vsoc::CuttlefishConfig& config) {
  return config.logcat_mode() == cvd::kLogcatVsockMode;
}

std::vector<cvd::SharedFD> LaunchKernelLogMonitor(
    const vsoc::CuttlefishConfig& config,
    cvd::ProcessMonitor* process_monitor,
    unsigned int number_of_event_pipes) {
  auto instance = config.ForDefaultInstance();
  auto log_name = instance.kernel_log_pipe_name();
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
  auto instance = config.ForDefaultInstance();

  std::string tombstoneDir = instance.PerInstancePath("tombstones");
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

StreamerLaunchResult LaunchVNCServer(
    const vsoc::CuttlefishConfig& config,
    cvd::ProcessMonitor* process_monitor,
    std::function<bool(MonitorEntry*)> callback) {
  auto instance = config.ForDefaultInstance();
  // Launch the vnc server, don't wait for it to complete
  auto port_options = "-port=" + std::to_string(instance.vnc_server_port());
  cvd::Command vnc_server(config.vnc_server_binary());
  vnc_server.AddParameter(port_options);

  auto server_ret = CreateStreamerServers(&vnc_server, config);

  process_monitor->StartSubprocess(std::move(vnc_server), callback);
  server_ret.launched = true;
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

StreamerLaunchResult LaunchWebRTC(cvd::ProcessMonitor* process_monitor,
                                  const vsoc::CuttlefishConfig& config) {
  cvd::Command webrtc(config.webrtc_binary());

  if (!config.webrtc_certs_dir().empty()) {
      webrtc.AddParameter("--certs_dir=", config.webrtc_certs_dir());
  }

  webrtc.AddParameter("--http_server_port=", vsoc::ForCurrentInstance(8443));
  webrtc.AddParameter("--public_ip=", config.webrtc_public_ip());
  webrtc.AddParameter("--assets_dir=", config.webrtc_assets_dir());

  auto server_ret = CreateStreamerServers(&webrtc, config);

  if (config.webrtc_enable_adb_websocket()) {
      auto instance = config.ForDefaultInstance();
      webrtc.AddParameter("--adb=", instance.adb_ip_and_port());
  }

  process_monitor->StartSubprocess(std::move(webrtc),
                                   GetOnSubprocessExitCallback(config));
  server_ret.launched = true;

  return server_ret;
}

void LaunchSocketVsockProxyIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  if (AdbVsockTunnelEnabled(config)) {
    cvd::Command adb_tunnel(config.socket_vsock_proxy_binary());
    adb_tunnel.AddParameter("--server=tcp");
    adb_tunnel.AddParameter("--vsock_port=6520");
    adb_tunnel.AddParameter(
        std::string{"--tcp_port="} + std::to_string(instance.host_port()));
    adb_tunnel.AddParameter(std::string{"--vsock_cid="} +
                            std::to_string(instance.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
  if (AdbVsockHalfTunnelEnabled(config)) {
    cvd::Command adb_tunnel(config.socket_vsock_proxy_binary());
    adb_tunnel.AddParameter("--server=tcp");
    adb_tunnel.AddParameter("--vsock_port=5555");
    adb_tunnel.AddParameter(
        std::string{"--tcp_port="} + std::to_string(instance.host_port()));
    adb_tunnel.AddParameter(std::string{"--vsock_cid="} +
                            std::to_string(instance.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
}

TpmPorts LaunchTpmSimulator(cvd::ProcessMonitor* process_monitor,
                            const vsoc::CuttlefishConfig& config) {
  int port = config.ForDefaultInstance().tpm_port();
  cvd::Command tpm_command(
      vsoc::DefaultHostArtifactsPath("bin/tpm_simulator_manager"));
  tpm_command.AddParameter("-port=", port);
  process_monitor->StartSubprocess(std::move(tpm_command),
                                   GetOnSubprocessExitCallback(config));

  cvd::Command proxy_command(config.socket_vsock_proxy_binary());
  proxy_command.AddParameter("--server=vsock");
  proxy_command.AddParameter("--tcp_port=", port);
  proxy_command.AddParameter("--vsock_port=", port);
  process_monitor->StartSubprocess(std::move(proxy_command),
                                   GetOnSubprocessExitCallback(config));
  return TpmPorts{port};
}

void LaunchMetrics(cvd::ProcessMonitor* process_monitor,
                                  const vsoc::CuttlefishConfig& config) {
  cvd::Command metrics(config.metrics_binary());

  process_monitor->StartSubprocess(std::move(metrics),
                                   GetOnSubprocessExitCallback(config));
}

TpmPorts LaunchTpmPassthrough(cvd::ProcessMonitor* process_monitor,
                              const vsoc::CuttlefishConfig& config) {
  auto server = cvd::SharedFD::VsockServer(SOCK_STREAM);
  if (!server->IsOpen()) {
    LOG(ERROR) << "Unable to create tpm passthrough server: "
               << server->StrError();
    std::exit(RunnerExitCodes::kTpmPassthroughError);
  }
  cvd::Command tpm_command(
      vsoc::DefaultHostArtifactsPath("bin/vtpm_passthrough"));
  tpm_command.AddParameter("-server_fd=", server);
  tpm_command.AddParameter("-device=", config.tpm_device());

  process_monitor->StartSubprocess(std::move(tpm_command),
                                   GetOnSubprocessExitCallback(config));

  return TpmPorts{server->VsockServerPort()};
}

TpmPorts LaunchTpm(cvd::ProcessMonitor* process_monitor,
                   const vsoc::CuttlefishConfig& config) {
  if (config.tpm_device() != "") {
    if (config.tpm_binary() != "") {
      LOG(WARNING) << "Both -tpm_device and -tpm_binary were set. Using -tpm_device.";
    }
    return LaunchTpmPassthrough(process_monitor, config);
  } else if (config.tpm_binary() != "") {
    return LaunchTpmSimulator(process_monitor, config);
  } else {
    return TpmPorts{};
  }
}
