#include "host/commands/run_cvd/launch.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/size_utils.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/config/known_paths.h"
#include "host/libs/vm_manager/crosvm_manager.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace cuttlefish {

using vm_manager::QemuManager;

namespace {

std::string GetAdbConnectorTcpArg(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return std::string{"0.0.0.0:"} + std::to_string(instance.host_port());
}

std::string GetAdbConnectorVsockArg(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return std::string{"vsock:"} + std::to_string(instance.vsock_guest_cid()) +
         std::string{":5555"};
}

bool AdbModeEnabled(const CuttlefishConfig& config, AdbMode mode) {
  return config.adb_mode().count(mode) > 0;
}

bool AdbVsockTunnelEnabled(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.vsock_guest_cid() > 2 &&
         AdbModeEnabled(config, AdbMode::VsockTunnel);
}

bool AdbVsockHalfTunnelEnabled(const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  return instance.vsock_guest_cid() > 2 &&
         AdbModeEnabled(config, AdbMode::VsockHalfTunnel);
}

bool AdbTcpConnectorEnabled(const CuttlefishConfig& config) {
  bool vsock_tunnel = AdbVsockTunnelEnabled(config);
  bool vsock_half_tunnel = AdbVsockHalfTunnelEnabled(config);
  return config.run_adb_connector() && (vsock_tunnel || vsock_half_tunnel);
}

bool AdbVsockConnectorEnabled(const CuttlefishConfig& config) {
  return config.run_adb_connector() &&
         AdbModeEnabled(config, AdbMode::NativeVsock);
}

OnSocketReadyCb GetOnSubprocessExitCallback(const CuttlefishConfig& config) {
  if (config.restart_subprocesses()) {
    return ProcessMonitor::RestartOnExitCb;
  } else {
    return ProcessMonitor::DoNotMonitorCb;
  }
}

SharedFD CreateUnixInputServer(const std::string& path) {
  auto server =
      SharedFD::SocketLocalServer(path.c_str(), false, SOCK_STREAM, 0666);
  if (!server->IsOpen()) {
    LOG(ERROR) << "Unable to create unix input server: " << server->StrError();
    return {};
  }
  return server;
}

// Creates the frame and input sockets and add the relevant arguments to the vnc
// server and webrtc commands
void CreateStreamerServers(Command* cmd, const CuttlefishConfig& config) {
  SharedFD touch_server;
  SharedFD keyboard_server;

  auto instance = config.ForDefaultInstance();
  if (config.vm_manager() == QemuManager::name()) {
    cmd->AddParameter("-write_virtio_input");

    touch_server = SharedFD::VsockServer(instance.touch_server_port(),
                                         SOCK_STREAM);
    keyboard_server = SharedFD::VsockServer(instance.keyboard_server_port(),
                                            SOCK_STREAM);
  } else {
    touch_server = CreateUnixInputServer(instance.touch_socket_path());
    keyboard_server = CreateUnixInputServer(instance.keyboard_socket_path());
  }
  if (!touch_server->IsOpen()) {
    LOG(ERROR) << "Could not open touch server: " << touch_server->StrError();
    return;
  }
  cmd->AddParameter("-touch_fd=", touch_server);

  if (!keyboard_server->IsOpen()) {
    LOG(ERROR) << "Could not open keyboard server: "
               << keyboard_server->StrError();
    return;
  }
  cmd->AddParameter("-keyboard_fd=", keyboard_server);

  SharedFD frames_server;
  if (config.gpu_mode() == kGpuModeDrmVirgl ||
      config.gpu_mode() == kGpuModeGfxStream) {
    frames_server = CreateUnixInputServer(instance.frames_socket_path());
  } else {
    frames_server = SharedFD::VsockServer(instance.frames_server_port(),
                                          SOCK_STREAM);
  }
  if (!frames_server->IsOpen()) {
    LOG(ERROR) << "Could not open frames server: " << frames_server->StrError();
    return;
  }
  cmd->AddParameter("-frame_server_fd=", frames_server);
}

}  // namespace

std::vector<SharedFD> LaunchKernelLogMonitor(
    const CuttlefishConfig& config, ProcessMonitor* process_monitor,
    unsigned int number_of_event_pipes) {
  auto instance = config.ForDefaultInstance();
  auto log_name = instance.kernel_log_pipe_name();
  if (mkfifo(log_name.c_str(), 0600) != 0) {
    LOG(ERROR) << "Unable to create named pipe at " << log_name << ": "
               << strerror(errno);
    return {};
  }

  SharedFD pipe;
  // Open the pipe here (from the launcher) to ensure the pipe is not deleted
  // due to the usage counters in the kernel reaching zero. If this is not done
  // and the kernel_log_monitor crashes for some reason the VMM may get SIGPIPE.
  pipe = SharedFD::Open(log_name.c_str(), O_RDWR);
  Command command(KernelLogMonitorBinary());
  command.AddParameter("-log_pipe_fd=", pipe);

  std::vector<SharedFD> ret;

  if (number_of_event_pipes > 0) {
    auto param_builder = command.GetParameterBuilder();
    param_builder << "-subscriber_fds=";
    for (unsigned int i = 0; i < number_of_event_pipes; ++i) {
      SharedFD event_pipe_write_end, event_pipe_read_end;
      if (!SharedFD::Pipe(&event_pipe_read_end, &event_pipe_write_end)) {
        LOG(ERROR) << "Unable to create kernel log events pipe: " << strerror(errno);
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

void LaunchLogcatReceiver(const CuttlefishConfig& config,
                          ProcessMonitor* process_monitor) {
  auto instance = config.ForDefaultInstance();
  auto log_name = instance.logcat_pipe_name();
  if (mkfifo(log_name.c_str(), 0600) != 0) {
    LOG(ERROR) << "Unable to create named pipe at " << log_name << ": "
               << strerror(errno);
    return;
  }

  SharedFD pipe;
  // Open the pipe here (from the launcher) to ensure the pipe is not deleted
  // due to the usage counters in the kernel reaching zero. If this is not done
  // and the logcat_receiver crashes for some reason the VMM may get SIGPIPE.
  pipe = SharedFD::Open(log_name.c_str(), O_RDWR);
  Command command(LogcatReceiverBinary());
  command.AddParameter("-log_pipe_fd=", pipe);

  process_monitor->StartSubprocess(std::move(command),
                                   GetOnSubprocessExitCallback(config));
  return;
}

void LaunchConfigServer(const CuttlefishConfig& config,
                        ProcessMonitor* process_monitor) {
  auto instance = config.ForDefaultInstance();
  auto port = instance.config_server_port();
  auto socket = SharedFD::VsockServer(port, SOCK_STREAM);
  if (!socket->IsOpen()) {
    LOG(ERROR) << "Unable to create configuration server socket: "
               << socket->StrError();
    std::exit(RunnerExitCodes::kConfigServerError);
  }
  Command cmd(ConfigServerBinary());
  cmd.AddParameter("-server_fd=", socket);
  process_monitor->StartSubprocess(std::move(cmd),
                                   GetOnSubprocessExitCallback(config));
  return;
}

void LaunchTombstoneReceiver(const CuttlefishConfig& config,
                             ProcessMonitor* process_monitor) {
  auto instance = config.ForDefaultInstance();

  std::string tombstoneDir = instance.PerInstancePath("tombstones");
  if (!DirectoryExists(tombstoneDir.c_str())) {
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
  auto socket = SharedFD::VsockServer(port, SOCK_STREAM);
  if (!socket->IsOpen()) {
    LOG(ERROR) << "Unable to create tombstone server socket: "
               << socket->StrError();
    std::exit(RunnerExitCodes::kTombstoneServerError);
    return;
  }
  Command cmd(TombstoneReceiverBinary());
  cmd.AddParameter("-server_fd=", socket);
  cmd.AddParameter("-tombstone_dir=", tombstoneDir);

  process_monitor->StartSubprocess(std::move(cmd),
                                   GetOnSubprocessExitCallback(config));
  return;
}

void LaunchVNCServer(
    const CuttlefishConfig& config, ProcessMonitor* process_monitor,
    std::function<bool(MonitorEntry*)> callback) {
  auto instance = config.ForDefaultInstance();
  // Launch the vnc server, don't wait for it to complete
  auto port_options = "-port=" + std::to_string(instance.vnc_server_port());
  Command vnc_server(VncServerBinary());
  vnc_server.AddParameter(port_options);

  CreateStreamerServers(&vnc_server, config);

  process_monitor->StartSubprocess(std::move(vnc_server), callback);
}

void LaunchAdbConnectorIfEnabled(ProcessMonitor* process_monitor,
                                 const CuttlefishConfig& config) {
  Command adb_connector(AdbConnectorBinary());
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

void LaunchWebRTC(ProcessMonitor* process_monitor,
                  const CuttlefishConfig& config,
                  SharedFD kernel_log_events_pipe) {
  if (config.ForDefaultInstance().start_webrtc_sig_server()) {
    Command sig_server(WebRtcSigServerBinary());
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
  cuttlefish::SharedFD client_socket;
  cuttlefish::SharedFD host_socket;
  CHECK(cuttlefish::SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0,
                                         &client_socket, &host_socket))
      << "Could not open command socket for webRTC";

  auto stopper = [host_socket = std::move(host_socket)](cuttlefish::Subprocess* proc) {
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    CHECK(host_socket->SetSockOpt(SOL_SOCKET, SO_RCVTIMEO, &timeout,
                                  sizeof(timeout)) == 0)
        << "Could not set receive timeout";

    cuttlefish::WriteAll(host_socket, "C");
    char response[1];
    int read_ret = host_socket->Read(response, sizeof(response));
    if (read_ret != 0) {
      LOG(ERROR) << "Failed to read response from webrtc";
    }
    cuttlefish::KillSubprocess(proc);
    return true;
  };

  cuttlefish::Command webrtc(cuttlefish::WebRtcBinary(),
                             cuttlefish::SubprocessStopper(stopper));

  CreateStreamerServers(&webrtc, config);

  webrtc.AddParameter("--command_fd=", client_socket);

  webrtc.AddParameter("-kernel_log_events_fd=", kernel_log_events_pipe);

  LaunchCustomActionServers(webrtc, process_monitor, config);

  // TODO get from launcher params
  process_monitor->StartSubprocess(std::move(webrtc),
                                   GetOnSubprocessExitCallback(config));
}

bool StopModemSimulator() {
  auto config = CuttlefishConfig::Get();
  auto instance = config->ForDefaultInstance();

  std::string monitor_socket_name = "modem_simulator";
  std::stringstream ss;
  ss << instance.host_port();
  monitor_socket_name.append(ss.str());
  auto monitor_sock = SharedFD::SocketLocalClient(
      monitor_socket_name.c_str(), true, SOCK_STREAM);
  if (!monitor_sock->IsOpen()) {
    LOG(ERROR) << "The connection to modem simulator is closed";
    return false;
  }
  std::string msg("STOP");
  if (monitor_sock->Write(msg.data(), msg.size()) < 0) {
    monitor_sock->Close();
    LOG(ERROR) << "Failed to send 'STOP' to modem simulator";
    return false;
  }
  char buf[64] = {0};
  if (monitor_sock->Read(buf, sizeof(buf)) <= 0) {
    monitor_sock->Close();
    LOG(ERROR) << "Failed to read message from modem simulator";
    return false;
  }
  if (strcmp(buf, "OK")) {
    monitor_sock->Close();
    LOG(ERROR) << "Read '" << buf << "' instead of 'OK' from modem simulator";
    return false;
  }

  return true;
}

void LaunchModemSimulatorIfEnabled(
    const CuttlefishConfig& config,
    ProcessMonitor* process_monitor) {
  if (!config.enable_modem_simulator()) {
    LOG(DEBUG) << "Modem simulator not enabled";
    return;
  }

  int instance_number = config.modem_simulator_instance_number();
  if (instance_number > 3 /* max value */ || instance_number < 0) {
    LOG(ERROR)
        << "Modem simulator instance number should range between 1 and 3";
    return;
  }

  Command cmd(
      ModemSimulatorBinary(), [](Subprocess* proc) {
        auto stopped = StopModemSimulator();
        if (stopped) {
          return true;
        }
        LOG(WARNING) << "Failed to stop modem simulator nicely, "
                     << "attempting to KILL";
        return KillSubprocess(proc);
      });

  auto sim_type = config.modem_simulator_sim_type();
  cmd.AddParameter(std::string{"-sim_type="} + std::to_string(sim_type));

  auto instance = config.ForDefaultInstance();
  auto ports = instance.modem_simulator_ports();
  auto param_builder = cmd.GetParameterBuilder();
  param_builder << "-server_fds=";
  for (int i = 0; i < instance_number; ++i) {
    auto pos = ports.find(',');
    auto temp = (pos != std::string::npos) ? ports.substr(0, pos - 1) : ports;
    auto port = std::stoi(temp);
    ports = ports.substr(pos + 1);

    auto socket = SharedFD::VsockServer(port, SOCK_STREAM);
    if (!socket->IsOpen()) {
      LOG(ERROR) << "Unable to create modem simulator server socket: "
                 << socket->StrError();
      std::exit(RunnerExitCodes::kModemSimulatorServerError);
    }
    if (i > 0) {
      param_builder << ",";
    }
    param_builder << socket;
  }
  param_builder.Build();

  process_monitor->StartSubprocess(std::move(cmd),
                                   GetOnSubprocessExitCallback(config));
}

void LaunchSocketVsockProxyIfEnabled(ProcessMonitor* process_monitor,
                                     const CuttlefishConfig& config,
                                     SharedFD adbd_events_pipe) {
  auto instance = config.ForDefaultInstance();
  auto append = [](const std::string& s, const int i) -> std::string {
    return s + std::to_string(i);
  };
  auto to_vsock_port = [&instance](const int port) -> int {
    return GetVsockServerPort(port, instance.vsock_guest_cid());
  };
  if (AdbVsockTunnelEnabled(config)) {
    Command adb_tunnel(SocketVsockProxyBinary());
    adb_tunnel.AddParameter("-adbd_events_fd=", adbd_events_pipe);
    adb_tunnel.AddParameter("--server=tcp");
    adb_tunnel.AddParameter(append("--vsock_port=", to_vsock_port(6520)));
    adb_tunnel.AddParameter(append("--tcp_port=", instance.host_port()));
    adb_tunnel.AddParameter(append("--vsock_cid=", instance.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
  if (AdbVsockHalfTunnelEnabled(config)) {
    Command adb_tunnel(SocketVsockProxyBinary());
    adb_tunnel.AddParameter("-adbd_events_fd=", adbd_events_pipe);
    adb_tunnel.AddParameter("--server=tcp");
    adb_tunnel.AddParameter(append("--vsock_port=", to_vsock_port(5555)));
    adb_tunnel.AddParameter(append("--tcp_port=", instance.host_port()));
    adb_tunnel.AddParameter(append("--vsock_cid=", instance.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
}

void LaunchMetrics(ProcessMonitor* process_monitor,
                   const CuttlefishConfig& config) {
  Command metrics(MetricsBinary());

  process_monitor->StartSubprocess(std::move(metrics),
                                   GetOnSubprocessExitCallback(config));
}

void LaunchGnssGrpcProxyServerIfEnabled(const CuttlefishConfig& config,
                                        ProcessMonitor* process_monitor) {
    if (!config.enable_gnss_grpc_proxy() ||
        !FileExists(GnssGrpcProxyBinary())) {
        return;
    }

    Command gnss_grpc_proxy_cmd(GnssGrpcProxyBinary());
    auto instance = config.ForDefaultInstance();

    auto gnss_in_pipe_name = instance.gnss_in_pipe_name();
    if (mkfifo(gnss_in_pipe_name.c_str(), 0600) != 0) {
      auto error = errno;
      LOG(ERROR) << "Failed to create gnss input fifo for crosvm: "
                << strerror(error);
      return;
    }

    auto gnss_out_pipe_name = instance.gnss_out_pipe_name();
    if (mkfifo(gnss_out_pipe_name.c_str(), 0660) != 0) {
      auto error = errno;
      LOG(ERROR) << "Failed to create gnss output fifo for crosvm: "
                << strerror(error);
      return;
    }

    // These fds will only be read from or written to, but open them with
    // read and write access to keep them open in case the subprocesses exit
    SharedFD gnss_grpc_proxy_in_wr =
        SharedFD::Open(gnss_in_pipe_name.c_str(), O_RDWR);
    if (!gnss_grpc_proxy_in_wr->IsOpen()) {
      LOG(ERROR) << "Failed to open gnss_grpc_proxy input fifo for writes: "
                << gnss_grpc_proxy_in_wr->StrError();
      return;
    }

    SharedFD gnss_grpc_proxy_out_rd =
        SharedFD::Open(gnss_out_pipe_name.c_str(), O_RDWR);
    if (!gnss_grpc_proxy_out_rd->IsOpen()) {
      LOG(ERROR) << "Failed to open gnss_grpc_proxy output fifo for reads: "
                << gnss_grpc_proxy_out_rd->StrError();
      return;
    }

    const unsigned gnss_grpc_proxy_server_port = instance.gnss_grpc_proxy_server_port();
    gnss_grpc_proxy_cmd.AddParameter("--gnss_in_fd=", gnss_grpc_proxy_in_wr);
    gnss_grpc_proxy_cmd.AddParameter("--gnss_out_fd=", gnss_grpc_proxy_out_rd);
    gnss_grpc_proxy_cmd.AddParameter("--gnss_grpc_port=", gnss_grpc_proxy_server_port);
    process_monitor->StartSubprocess(std::move(gnss_grpc_proxy_cmd),
                                     GetOnSubprocessExitCallback(config));
}

void LaunchSecureEnvironment(ProcessMonitor* process_monitor,
                             const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  std::vector<std::string> fifo_paths = {
    instance.PerInstanceInternalPath("keymaster_fifo_vm.in"),
    instance.PerInstanceInternalPath("keymaster_fifo_vm.out"),
    instance.PerInstanceInternalPath("gatekeeper_fifo_vm.in"),
    instance.PerInstanceInternalPath("gatekeeper_fifo_vm.out"),
  };
  std::vector<SharedFD> fifos;
  for (const auto& path : fifo_paths) {
    unlink(path.c_str());
    if (mkfifo(path.c_str(), 0600) < 0) {
      PLOG(ERROR) << "Could not create " << path;
      return;
    }
    auto fd = SharedFD::Open(path, O_RDWR);
    if (!fd->IsOpen()) {
      LOG(ERROR) << "Could not open " << path << ": " << fd->StrError();
      return;
    }
    fifos.push_back(fd);
  }

  Command command(DefaultHostArtifactsPath("bin/secure_env"));
  command.AddParameter("-keymaster_fd_out=", fifos[0]);
  command.AddParameter("-keymaster_fd_in=", fifos[1]);
  command.AddParameter("-gatekeeper_fd_out=", fifos[2]);
  command.AddParameter("-gatekeeper_fd_in=", fifos[3]);
  process_monitor->StartSubprocess(std::move(command),
                                   GetOnSubprocessExitCallback(config));
}

void LaunchCustomActionServers(Command& webrtc_cmd,
                               ProcessMonitor* process_monitor,
                               const CuttlefishConfig& config) {
  bool first = true;
  for (const auto& custom_action : config.custom_actions()) {
    if (custom_action.server) {
      // Create a socket pair that will be used for communication between
      // WebRTC and the action server.
      SharedFD webrtc_socket, action_server_socket;
      if (!SharedFD::SocketPair(AF_LOCAL, SOCK_STREAM, 0,
                                &webrtc_socket, &action_server_socket)) {
        LOG(ERROR) << "Unable to create custom action server socket pair: "
                   << strerror(errno);
        continue;
      }

      // Launch the action server, providing its socket pair fd as the only argument.
      std::string binary = "bin/" + *(custom_action.server);
      Command command(DefaultHostArtifactsPath(binary));
      command.AddParameter(action_server_socket);
      process_monitor->StartSubprocess(std::move(command),
                                       GetOnSubprocessExitCallback(config));

      // Pass the WebRTC socket pair fd to WebRTC.
      if (first) {
        first = false;
        webrtc_cmd.AddParameter("-action_servers=", *custom_action.server, ":", webrtc_socket);
      } else {
        webrtc_cmd.AppendToLastParameter(",", *custom_action.server, ":", webrtc_socket);
      }
    }
  }
}

void LaunchVehicleHalServerIfEnabled(const CuttlefishConfig& config,
                                      ProcessMonitor* process_monitor) {
  if (!config.enable_vehicle_hal_grpc_server() ||
    !FileExists(config.vehicle_hal_grpc_server_binary())) {
    return;
  }

  Command grpc_server(config.vehicle_hal_grpc_server_binary());
  auto instance = config.ForDefaultInstance();

  const unsigned vhal_server_cid = 2;
  const unsigned vhal_server_port = instance.vehicle_hal_server_port();
  const std::string vhal_server_power_state_file =
      AbsolutePath(instance.PerInstancePath("power_state"));
  const std::string vhal_server_power_state_socket =
      AbsolutePath(instance.PerInstancePath("power_state_socket"));

  grpc_server.AddParameter("--server_cid=", vhal_server_cid);
  grpc_server.AddParameter("--server_port=", vhal_server_port);
  grpc_server.AddParameter("--power_state_file=", vhal_server_power_state_file);
  grpc_server.AddParameter("--power_state_socket=", vhal_server_power_state_socket);
  process_monitor->StartSubprocess(std::move(grpc_server),
                                   GetOnSubprocessExitCallback(config));
}

void LaunchConsoleForwarderIfEnabled(const CuttlefishConfig& config,
                                     ProcessMonitor* process_monitor)
{
  if (!config.console()) {
    return;
  }

  Command console_forwarder_cmd(ConsoleForwarderBinary());
  auto instance = config.ForDefaultInstance();

  auto console_in_pipe_name = instance.console_in_pipe_name();
  if (mkfifo(console_in_pipe_name.c_str(), 0600) != 0) {
    auto error = errno;
    LOG(ERROR) << "Failed to create console input fifo for crosvm: "
               << strerror(error);
    return;
  }

  auto console_out_pipe_name = instance.console_out_pipe_name();
  if (mkfifo(console_out_pipe_name.c_str(), 0660) != 0) {
    auto error = errno;
    LOG(ERROR) << "Failed to create console output fifo for crosvm: "
               << strerror(error);
    return;
  }

  // These fds will only be read from or written to, but open them with
  // read and write access to keep them open in case the subprocesses exit
  SharedFD console_forwarder_in_wr =
      SharedFD::Open(console_in_pipe_name.c_str(), O_RDWR);
  if (!console_forwarder_in_wr->IsOpen()) {
    LOG(ERROR) << "Failed to open console_forwarder input fifo for writes: "
               << console_forwarder_in_wr->StrError();
    return;
  }

  SharedFD console_forwarder_out_rd =
      SharedFD::Open(console_out_pipe_name.c_str(), O_RDWR);
  if (!console_forwarder_out_rd->IsOpen()) {
    LOG(ERROR) << "Failed to open console_forwarder output fifo for reads: "
               << console_forwarder_out_rd->StrError();
    return;
  }

  console_forwarder_cmd.AddParameter("--console_in_fd=", console_forwarder_in_wr);
  console_forwarder_cmd.AddParameter("--console_out_fd=", console_forwarder_out_rd);
  process_monitor->StartSubprocess(std::move(console_forwarder_cmd),
                                   GetOnSubprocessExitCallback(config));

}

} // namespace cuttlefish
