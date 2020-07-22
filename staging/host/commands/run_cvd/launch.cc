#include "host/commands/run_cvd/launch.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/size_utils.h"
#include "host/commands/run_cvd/pre_launch_initializers.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

using cuttlefish::MonitorEntry;
using cuttlefish::RunnerExitCodes;
using cuttlefish::vm_manager::QemuManager;

namespace {

std::string GetAdbConnectorTcpArg(const cuttlefish::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return std::string{"127.0.0.1:"} + std::to_string(instance.host_port());
}

std::string GetAdbConnectorVsockArg(const cuttlefish::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return std::string{"vsock:"} + std::to_string(instance.vsock_guest_cid()) +
         std::string{":5555"};
}

bool AdbModeEnabled(const cuttlefish::CuttlefishConfig& config, cuttlefish::AdbMode mode) {
  return config.adb_mode().count(mode) > 0;
}

bool AdbVsockTunnelEnabled(const cuttlefish::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.vsock_guest_cid() > 2 &&
         AdbModeEnabled(config, cuttlefish::AdbMode::VsockTunnel);
}

bool AdbVsockHalfTunnelEnabled(const cuttlefish::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.vsock_guest_cid() > 2 &&
         AdbModeEnabled(config, cuttlefish::AdbMode::VsockHalfTunnel);
}

bool AdbTcpConnectorEnabled(const cuttlefish::CuttlefishConfig& config) {
  bool vsock_tunnel = AdbVsockTunnelEnabled(config);
  bool vsock_half_tunnel = AdbVsockHalfTunnelEnabled(config);
  return config.run_adb_connector() && (vsock_tunnel || vsock_half_tunnel);
}

bool AdbVsockConnectorEnabled(const cuttlefish::CuttlefishConfig& config) {
  return config.run_adb_connector() &&
         AdbModeEnabled(config, cuttlefish::AdbMode::NativeVsock);
}

cuttlefish::OnSocketReadyCb GetOnSubprocessExitCallback(
    const cuttlefish::CuttlefishConfig& config) {
  if (config.restart_subprocesses()) {
    return cuttlefish::ProcessMonitor::RestartOnExitCb;
  } else {
    return cuttlefish::ProcessMonitor::DoNotMonitorCb;
  }
}

cuttlefish::SharedFD CreateUnixInputServer(const std::string& path) {
  auto server =
      cuttlefish::SharedFD::SocketLocalServer(path.c_str(), false, SOCK_STREAM, 0666);
  if (!server->IsOpen()) {
    LOG(ERROR) << "Unable to create unix input server: " << server->StrError();
    return cuttlefish::SharedFD();
  }
  return server;
}

// Creates the frame and input sockets and add the relevant arguments to the vnc
// server and webrtc commands
StreamerLaunchResult CreateStreamerServers(
    cuttlefish::Command* cmd, const cuttlefish::CuttlefishConfig& config) {
  StreamerLaunchResult server_ret;
  cuttlefish::SharedFD touch_server;
  cuttlefish::SharedFD keyboard_server;

  auto instance = config.ForDefaultInstance();
  if (config.vm_manager() == QemuManager::name()) {
    cmd->AddParameter("-write_virtio_input");

    touch_server = cuttlefish::SharedFD::VsockServer(instance.touch_server_port(),
                                              SOCK_STREAM);
    keyboard_server =
        cuttlefish::SharedFD::VsockServer(instance.keyboard_server_port(),
                                   SOCK_STREAM);
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
    LOG(ERROR) << "Could not open keyboard server: "
               << keyboard_server->StrError();
    return {};
  }
  cmd->AddParameter("-keyboard_fd=", keyboard_server);

  cuttlefish::SharedFD frames_server;
  if (config.gpu_mode() == cuttlefish::kGpuModeDrmVirgl ||
      config.gpu_mode() == cuttlefish::kGpuModeGfxStream) {
    frames_server = CreateUnixInputServer(instance.frames_socket_path());
  } else {
    frames_server = cuttlefish::SharedFD::VsockServer(instance.frames_server_port(),
                                               SOCK_STREAM);
  }
  if (!frames_server->IsOpen()) {
    LOG(ERROR) << "Could not open frames server: " << frames_server->StrError();
    return {};
  }
  cmd->AddParameter("-frame_server_fd=", frames_server);
  return server_ret;
}

}  // namespace

std::vector<cuttlefish::SharedFD> LaunchKernelLogMonitor(
    const cuttlefish::CuttlefishConfig& config, cuttlefish::ProcessMonitor* process_monitor,
    unsigned int number_of_event_pipes) {
  auto instance = config.ForDefaultInstance();
  auto log_name = instance.kernel_log_pipe_name();
  if (mkfifo(log_name.c_str(), 0600) != 0) {
    LOG(ERROR) << "Unable to create named pipe at " << log_name << ": "
               << strerror(errno);
    return {};
  }

  cuttlefish::SharedFD pipe;
  // Open the pipe here (from the launcher) to ensure the pipe is not deleted
  // due to the usage counters in the kernel reaching zero. If this is not done
  // and the kernel_log_monitor crashes for some reason the VMM may get SIGPIPE.
  pipe = cuttlefish::SharedFD::Open(log_name.c_str(), O_RDWR);
  cuttlefish::Command command(config.kernel_log_monitor_binary());
  command.AddParameter("-log_pipe_fd=", pipe);

  std::vector<cuttlefish::SharedFD> ret;

  if (number_of_event_pipes > 0) {
    auto param_builder = command.GetParameterBuilder();
    param_builder << "-subscriber_fds=";
    for (unsigned int i = 0; i < number_of_event_pipes; ++i) {
      cuttlefish::SharedFD event_pipe_write_end, event_pipe_read_end;
      if (!cuttlefish::SharedFD::Pipe(&event_pipe_read_end, &event_pipe_write_end)) {
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

void LaunchLogcatReceiver(const cuttlefish::CuttlefishConfig& config,
                          cuttlefish::ProcessMonitor* process_monitor) {
  auto instance = config.ForDefaultInstance();
  auto log_name = instance.logcat_pipe_name();
  if (mkfifo(log_name.c_str(), 0600) != 0) {
    LOG(ERROR) << "Unable to create named pipe at " << log_name << ": "
               << strerror(errno);
    return;
  }

  cuttlefish::SharedFD pipe;
  // Open the pipe here (from the launcher) to ensure the pipe is not deleted
  // due to the usage counters in the kernel reaching zero. If this is not done
  // and the logcat_receiver crashes for some reason the VMM may get SIGPIPE.
  pipe = cuttlefish::SharedFD::Open(log_name.c_str(), O_RDWR);
  cuttlefish::Command command(config.logcat_receiver_binary());
  command.AddParameter("-log_pipe_fd=", pipe);

  process_monitor->StartSubprocess(std::move(command),
                                   GetOnSubprocessExitCallback(config));
  return;
}

void LaunchConfigServer(const cuttlefish::CuttlefishConfig& config,
                        cuttlefish::ProcessMonitor* process_monitor) {
  auto instance = config.ForDefaultInstance();
  auto port = instance.config_server_port();
  auto socket = cuttlefish::SharedFD::VsockServer(port, SOCK_STREAM);
  if (!socket->IsOpen()) {
    LOG(ERROR) << "Unable to create configuration server socket: "
               << socket->StrError();
    std::exit(RunnerExitCodes::kConfigServerError);
  }
  cuttlefish::Command cmd(config.config_server_binary());
  cmd.AddParameter("-server_fd=", socket);
  process_monitor->StartSubprocess(std::move(cmd),
                                   GetOnSubprocessExitCallback(config));
  return;
}

void LaunchTombstoneReceiverIfEnabled(const cuttlefish::CuttlefishConfig& config,
                                      cuttlefish::ProcessMonitor* process_monitor) {
  if (!config.enable_tombstone_receiver()) {
    return;
  }
  auto instance = config.ForDefaultInstance();

  std::string tombstoneDir = instance.PerInstancePath("tombstones");
  if (!cuttlefish::DirectoryExists(tombstoneDir.c_str())) {
    LOG(DEBUG) << "Setting up " << tombstoneDir;
    if (mkdir(tombstoneDir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) <
        0) {
      LOG(ERROR) << "Failed to create tombstone directory: " << tombstoneDir
                 << ". Error: " << errno;
      exit(RunnerExitCodes::kTombstoneDirCreationError);
      return;
    }
  }

  auto port = instance.tombstone_receiver_port();
  auto socket = cuttlefish::SharedFD::VsockServer(port, SOCK_STREAM);
  if (!socket->IsOpen()) {
    LOG(ERROR) << "Unable to create tombstone server socket: "
               << socket->StrError();
    std::exit(RunnerExitCodes::kTombstoneServerError);
    return;
  }
  cuttlefish::Command cmd(config.tombstone_receiver_binary());
  cmd.AddParameter("-server_fd=", socket);
  cmd.AddParameter("-tombstone_dir=", tombstoneDir);

  process_monitor->StartSubprocess(std::move(cmd),
                                   GetOnSubprocessExitCallback(config));
  return;
}

StreamerLaunchResult LaunchVNCServer(
    const cuttlefish::CuttlefishConfig& config, cuttlefish::ProcessMonitor* process_monitor,
    std::function<bool(MonitorEntry*)> callback) {
  auto instance = config.ForDefaultInstance();
  // Launch the vnc server, don't wait for it to complete
  auto port_options = "-port=" + std::to_string(instance.vnc_server_port());
  cuttlefish::Command vnc_server(config.vnc_server_binary());
  vnc_server.AddParameter(port_options);

  auto server_ret = CreateStreamerServers(&vnc_server, config);

  process_monitor->StartSubprocess(std::move(vnc_server), callback);
  server_ret.launched = true;
  return server_ret;
}

void LaunchAdbConnectorIfEnabled(cuttlefish::ProcessMonitor* process_monitor,
                                 const cuttlefish::CuttlefishConfig& config,
                                 cuttlefish::SharedFD adbd_events_pipe) {
  cuttlefish::Command adb_connector(config.adb_connector_binary());
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

StreamerLaunchResult LaunchWebRTC(cuttlefish::ProcessMonitor* process_monitor,
                                  const cuttlefish::CuttlefishConfig& config) {
  if (config.ForDefaultInstance().start_webrtc_sig_server()) {
    cuttlefish::Command sig_server(config.sig_server_binary());
    sig_server.AddParameter("-assets_dir=", config.webrtc_assets_dir());
    if (!config.webrtc_certs_dir().empty()) {
      sig_server.AddParameter("-certs_dir=", config.webrtc_certs_dir());
    }
    sig_server.AddParameter("-http_server_port=", config.sig_server_port());
    process_monitor->StartSubprocess(std::move(sig_server),
                                     GetOnSubprocessExitCallback(config));
  }

  // Currently there is no way to ensure the signaling server will already have
  // bound the socket to the port by the time the webrtc process runs (the
  // common technique of doing it from the launcher is not possible here as the
  // server library being used creates its own sockets). However, this issue is
  // mitigated slightly by doing some retrying and backoff in the webrtc process
  // when connecting to the websocket, so it shouldn't be an issue most of the
  // time.

  cuttlefish::Command webrtc(config.webrtc_binary());
  webrtc.AddParameter("-public_ip=", config.webrtc_public_ip());

  auto server_ret = CreateStreamerServers(&webrtc, config);

  if (config.webrtc_enable_adb_websocket()) {
    auto instance = config.ForDefaultInstance();
    webrtc.AddParameter("--adb=", instance.adb_ip_and_port());
  }

  // TODO get from launcher params
  process_monitor->StartSubprocess(std::move(webrtc),
                                   GetOnSubprocessExitCallback(config));
  server_ret.launched = true;

  return server_ret;
}

void LaunchSocketVsockProxyIfEnabled(cuttlefish::ProcessMonitor* process_monitor,
                                     const cuttlefish::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  if (AdbVsockTunnelEnabled(config)) {
    cuttlefish::Command adb_tunnel(config.socket_vsock_proxy_binary());
    adb_tunnel.AddParameter("--server=tcp");
    adb_tunnel.AddParameter("--vsock_port=6520");
    adb_tunnel.AddParameter(std::string{"--tcp_port="} +
                            std::to_string(instance.host_port()));
    adb_tunnel.AddParameter(std::string{"--vsock_cid="} +
                            std::to_string(instance.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
  if (AdbVsockHalfTunnelEnabled(config)) {
    cuttlefish::Command adb_tunnel(config.socket_vsock_proxy_binary());
    adb_tunnel.AddParameter("--server=tcp");
    adb_tunnel.AddParameter("--vsock_port=5555");
    adb_tunnel.AddParameter(std::string{"--tcp_port="} +
                            std::to_string(instance.host_port()));
    adb_tunnel.AddParameter(std::string{"--vsock_cid="} +
                            std::to_string(instance.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
}

void LaunchTpmSimulator(cuttlefish::ProcessMonitor* process_monitor,
                   const cuttlefish::CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  auto port = instance.tpm_port();
  auto socket = cuttlefish::SharedFD::VsockServer(port, SOCK_STREAM);
  cuttlefish::Command tpm_command(
      cuttlefish::DefaultHostArtifactsPath("bin/tpm_simulator_manager"));
  tpm_command.AddParameter("-port=", port);
  process_monitor->StartSubprocess(std::move(tpm_command),
                                   GetOnSubprocessExitCallback(config));

  cuttlefish::Command proxy_command(config.socket_vsock_proxy_binary());
  proxy_command.AddParameter("--server=vsock");
  proxy_command.AddParameter("--tcp_port=", port);
  proxy_command.AddParameter("--vsock_port=", port);
  process_monitor->StartSubprocess(std::move(proxy_command),
                                   GetOnSubprocessExitCallback(config));
}

void LaunchMetrics(cuttlefish::ProcessMonitor* process_monitor,
                   const cuttlefish::CuttlefishConfig& config) {
  cuttlefish::Command metrics(config.metrics_binary());

  process_monitor->StartSubprocess(std::move(metrics),
                                   GetOnSubprocessExitCallback(config));
}

void LaunchTpmPassthrough(cuttlefish::ProcessMonitor* process_monitor,
                          const cuttlefish::CuttlefishConfig& config) {
  auto server = cuttlefish::SharedFD::VsockServer(SOCK_STREAM);
  if (!server->IsOpen()) {
    LOG(ERROR) << "Unable to create tpm passthrough server: "
               << server->StrError();
    std::exit(RunnerExitCodes::kTpmPassthroughError);
  }
  cuttlefish::Command tpm_command(
      cuttlefish::DefaultHostArtifactsPath("bin/vtpm_passthrough"));
  tpm_command.AddParameter("-server_fd=", server);
  tpm_command.AddParameter("-device=", config.tpm_device());

  process_monitor->StartSubprocess(std::move(tpm_command),
                                   GetOnSubprocessExitCallback(config));
}

void LaunchTpm(cuttlefish::ProcessMonitor* process_monitor,
               const cuttlefish::CuttlefishConfig& config) {
  if (config.tpm_device() != "") {
    if (config.tpm_binary() != "") {
      LOG(WARNING)
          << "Both -tpm_device and -tpm_binary were set. Using -tpm_device.";
    }
    LaunchTpmPassthrough(process_monitor, config);
  } else if (config.tpm_binary() != "") {
    LaunchTpmSimulator(process_monitor, config);
  }
}

void LaunchSecureEnvironment(cuttlefish::ProcessMonitor* process_monitor,
                             const cuttlefish::CuttlefishConfig& config) {
  auto keymaster_port = config.ForDefaultInstance().keymaster_vsock_port();
  auto keymaster_server =
      cuttlefish::SharedFD::VsockServer(keymaster_port, SOCK_STREAM);
  auto gatekeeper_port = config.ForDefaultInstance().gatekeeper_vsock_port();
  auto gatekeeper_server =
      cuttlefish::SharedFD::VsockServer(gatekeeper_port, SOCK_STREAM);
  cuttlefish::Command command(cuttlefish::DefaultHostArtifactsPath("bin/secure_env"));
  command.AddParameter("-keymaster_fd=", keymaster_server);
  command.AddParameter("-gatekeeper_fd=", gatekeeper_server);
  process_monitor->StartSubprocess(std::move(command),
                                   GetOnSubprocessExitCallback(config));
}

void LaunchVerhicleHalServerIfEnabled(const cuttlefish::CuttlefishConfig& config,
                                                        cuttlefish::ProcessMonitor* process_monitor) {
    if (!config.enable_vehicle_hal_grpc_server() ||
        !cuttlefish::FileExists(config.vehicle_hal_grpc_server_binary())) {
        return;
    }

    cuttlefish::Command grpc_server(config.vehicle_hal_grpc_server_binary());
    auto instance = config.ForDefaultInstance();

    const unsigned vhal_server_cid = 2;
    const unsigned vhal_server_port = instance.vehicle_hal_server_port();
    const std::string vhal_server_power_state_file =
        cuttlefish::AbsolutePath(instance.PerInstancePath("power_state"));
    const std::string vhal_server_power_state_socket =
        cuttlefish::AbsolutePath(instance.PerInstancePath("power_state_socket"));

    grpc_server.AddParameter("--server_cid=", vhal_server_cid);
    grpc_server.AddParameter("--server_port=", vhal_server_port);
    grpc_server.AddParameter("--power_state_file=", vhal_server_power_state_file);
    grpc_server.AddParameter("--power_state_socket=", vhal_server_power_state_socket);
    process_monitor->StartSubprocess(std::move(grpc_server),
                                     GetOnSubprocessExitCallback(config));
}
