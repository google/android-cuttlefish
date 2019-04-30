#include "host/commands/launch/launch.h"

#include <glog/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/size_utils.h"
#include "common/vsoc/shm/screen_layout.h"
#include "host/commands/launch/launcher_defs.h"
#include "host/commands/launch/pre_launch_initializers.h"
#include "host/commands/launch/vsoc_shared_memory.h"

using cvd::LauncherExitCodes;
using cvd::MonitorEntry;

namespace {

constexpr char kAdbModeTunnel[] = "tunnel";
constexpr char kAdbModeNativeVsock[] = "native_vsock";
constexpr char kAdbModeVsockTunnel[] = "vsock_tunnel";
constexpr char kAdbModeVsockHalfTunnel[] = "vsock_half_tunnel";
constexpr char kAdbModeUsb[] = "usb";

cvd::SharedFD CreateIvServerUnixSocket(const std::string& path) {
  return cvd::SharedFD::SocketLocalServer(path.c_str(), false, SOCK_STREAM,
                                          0666);
}

std::string GetGuestPortArg() {
  constexpr int kEmulatorPort = 5555;
  return std::string{"--guest_ports="} + std::to_string(kEmulatorPort);
}

std::string GetHostPortArg() {
  return std::string{"--host_ports="} + std::to_string(GetHostPort());
}

std::string GetAdbConnectorTcpArg() {
  return std::string{"--addresses=127.0.0.1:"} + std::to_string(GetHostPort());
}

std::string GetAdbConnectorVsockArg(const vsoc::CuttlefishConfig& config) {
  return std::string{"--addresses=vsock:"}
      + std::to_string(config.vsock_guest_cid())
      + std::string{":5555"};
}

bool AdbModeEnabled(const vsoc::CuttlefishConfig& config, const char* mode) {
  return config.adb_mode().count(mode) > 0;
}

bool AdbTunnelEnabled(const vsoc::CuttlefishConfig& config) {
  return AdbModeEnabled(config, kAdbModeTunnel);
}

bool AdbVsockTunnelEnabled(const vsoc::CuttlefishConfig& config) {
  return config.vsock_guest_cid() > 2
      && AdbModeEnabled(config, kAdbModeVsockTunnel);
}

bool AdbVsockHalfTunnelEnabled(const vsoc::CuttlefishConfig& config) {
  return config.vsock_guest_cid() > 2
      && AdbModeEnabled(config, kAdbModeVsockHalfTunnel);
}

bool AdbTcpConnectorEnabled(const vsoc::CuttlefishConfig& config) {
  bool tunnel = AdbTunnelEnabled(config);
  bool vsock_tunnel = AdbVsockTunnelEnabled(config);
  bool vsock_half_tunnel = AdbVsockHalfTunnelEnabled(config);
  return config.run_adb_connector()
      && (tunnel || vsock_tunnel || vsock_half_tunnel);
}

bool AdbVsockConnectorEnabled(const vsoc::CuttlefishConfig& config) {
  return config.run_adb_connector()
      && AdbModeEnabled(config, kAdbModeNativeVsock);
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

int GetHostPort() {
  constexpr int kFirstHostPort = 6520;
  return vsoc::GetPerInstanceDefault(kFirstHostPort);
}

bool LogcatReceiverEnabled(const vsoc::CuttlefishConfig& config) {
  return config.logcat_mode() == cvd::kLogcatVsockMode;
}

bool AdbUsbEnabled(const vsoc::CuttlefishConfig& config) {
  return AdbModeEnabled(config, kAdbModeUsb);
}

void ValidateAdbModeFlag(const vsoc::CuttlefishConfig& config) {
  if (!AdbUsbEnabled(config) && !AdbTunnelEnabled(config)
      && !AdbVsockTunnelEnabled(config) && !AdbVsockHalfTunnelEnabled(config)) {
    LOG(INFO) << "ADB not enabled";
  }
}

cvd::Command GetIvServerCommand(const vsoc::CuttlefishConfig& config) {
  // Resize screen region
  auto actual_width = cvd::AlignToPowerOf2(config.x_res() * 4, 4);// align to 16
  uint32_t screen_buffers_size =
      config.num_screen_buffers() *
      cvd::AlignToPageSize(actual_width * config.y_res() + 16 /* padding */);
  screen_buffers_size +=
      (config.num_screen_buffers() - 1) * 4096; /* Guard pages */

  // TODO(b/79170615) Resize gralloc region too.

  vsoc::CreateSharedMemoryFile(
      config.mempath(),
      {{vsoc::layout::screen::ScreenLayout::region_name, screen_buffers_size}});


  cvd::Command ivserver(config.ivserver_binary());
  ivserver.AddParameter(
      "-qemu_socket_fd=",
      CreateIvServerUnixSocket(config.ivshmem_qemu_socket_path()));
  ivserver.AddParameter(
      "-client_socket_fd=",
      CreateIvServerUnixSocket(config.ivshmem_client_socket_path()));
  return ivserver;
}

// Build the kernel log monitor command. If boot_event_pipe is not NULL, a
// subscription to boot events from the kernel log monitor will be created and
// events will appear on *boot_events_pipe
cvd::Command GetKernelLogMonitorCommand(const vsoc::CuttlefishConfig& config,
                                        cvd::SharedFD* boot_events_pipe) {
  auto log_name = config.kernel_log_socket_name();
  auto server = cvd::SharedFD::SocketLocalServer(log_name.c_str(), false,
                                                 SOCK_STREAM, 0666);
  cvd::Command kernel_log_monitor(config.kernel_log_monitor_binary());
  kernel_log_monitor.AddParameter("-log_server_fd=", server);
  if (boot_events_pipe) {
    cvd::SharedFD pipe_write_end;
    if (!cvd::SharedFD::Pipe(boot_events_pipe, &pipe_write_end)) {
      LOG(ERROR) << "Unable to create boot events pipe: " << strerror(errno);
      std::exit(LauncherExitCodes::kPipeIOError);
    }
    kernel_log_monitor.AddParameter("-subscriber_fd=", pipe_write_end);
  }
  return kernel_log_monitor;
}

void LaunchLogcatReceiverIfEnabled(const vsoc::CuttlefishConfig& config,
                                   cvd::ProcessMonitor* process_monitor) {
  if (!LogcatReceiverEnabled(config)) {
    return;
  }
  auto port = config.logcat_vsock_port();
  auto socket = cvd::SharedFD::VsockServer(port, SOCK_STREAM);
  if (!socket->IsOpen()) {
    LOG(ERROR) << "Unable to create logcat server socket: "
               << socket->StrError();
    std::exit(LauncherExitCodes::kLogcatServerError);
  }
  cvd::Command cmd(config.logcat_receiver_binary());
  cmd.AddParameter("-server_fd=", socket);
  process_monitor->StartSubprocess(std::move(cmd),
                                   GetOnSubprocessExitCallback(config));
}

void LaunchUsbServerIfEnabled(const vsoc::CuttlefishConfig& config,
                              cvd::ProcessMonitor* process_monitor) {
  if (!AdbUsbEnabled(config)) {
    return;
  }
  auto socket_name = config.usb_v1_socket_name();
  auto usb_v1_server = cvd::SharedFD::SocketLocalServer(
      socket_name.c_str(), false, SOCK_STREAM, 0666);
  if (!usb_v1_server->IsOpen()) {
    LOG(ERROR) << "Unable to create USB v1 server socket: "
               << usb_v1_server->StrError();
    std::exit(cvd::LauncherExitCodes::kUsbV1SocketError);
  }
  cvd::Command usb_server(config.virtual_usb_manager_binary());
  usb_server.AddParameter("-usb_v1_fd=", usb_v1_server);
  process_monitor->StartSubprocess(std::move(usb_server),
                                   GetOnSubprocessExitCallback(config));
}

cvd::SharedFD CreateVncInputServer(const std::string& path) {
  auto server = cvd::SharedFD::SocketLocalServer(path.c_str(), false, SOCK_STREAM, 0666);
  if (!server->IsOpen()) {
    LOG(ERROR) << "Unable to create mouse server: "
               << server->StrError();
    return cvd::SharedFD();
  }
  return server;
}

void LaunchVNCServerIfEnabled(const vsoc::CuttlefishConfig& config,
                              cvd::ProcessMonitor* process_monitor,
                              std::function<bool(MonitorEntry*)> callback) {
  if (config.enable_vnc_server()) {
    // Launch the vnc server, don't wait for it to complete
    auto port_options = "-port=" + std::to_string(config.vnc_server_port());
    cvd::Command vnc_server(config.vnc_server_binary());
    vnc_server.AddParameter(port_options);
    if (!config.enable_ivserver()) {
      // When the ivserver is not enabled, the vnc touch_server needs to serve
      // on unix sockets and send input events to whoever connects to it (namely
      // crosvm)
      auto touch_server = CreateVncInputServer(config.touch_socket_path());
      if (!touch_server->IsOpen()) {
        return;
      }
      vnc_server.AddParameter("-touch_fd=", touch_server);

      auto keyboard_server =
          CreateVncInputServer(config.keyboard_socket_path());
      if (!keyboard_server->IsOpen()) {
        return;
      }
      vnc_server.AddParameter("-keyboard_fd=", keyboard_server);
      // TODO(b/128852363): This should be handled through the wayland mock
      //  instead.
      // Additionally it receives the frame updates from a virtual socket
      // instead
      auto frames_server =
          cvd::SharedFD::VsockServer(config.frames_vsock_port(), SOCK_STREAM);
      if (!frames_server->IsOpen()) {
        return;
      }
      vnc_server.AddParameter("-frame_server_fd=", frames_server);
    }
    process_monitor->StartSubprocess(std::move(vnc_server), callback);
  }
}

void LaunchStreamAudioIfEnabled(const vsoc::CuttlefishConfig& config,
                                cvd::ProcessMonitor* process_monitor,
                                std::function<bool(MonitorEntry*)> callback) {
  if (config.enable_stream_audio()) {
    auto port_options = "-port=" + std::to_string(config.stream_audio_port());
    cvd::Command stream_audio(config.stream_audio_binary());
    stream_audio.AddParameter(port_options);
    process_monitor->StartSubprocess(std::move(stream_audio), callback);
  }
}

void LaunchAdbConnectorIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config) {
  if (AdbTcpConnectorEnabled(config)) {
    cvd::Command adb_connector(config.adb_connector_binary());
    adb_connector.AddParameter(GetAdbConnectorTcpArg());
    process_monitor->StartSubprocess(std::move(adb_connector),
                                     GetOnSubprocessExitCallback(config));
  }
  if (AdbVsockConnectorEnabled(config)) {
    cvd::Command adb_connector(config.adb_connector_binary());
    adb_connector.AddParameter(GetAdbConnectorVsockArg(config));
    process_monitor->StartSubprocess(std::move(adb_connector),
                                     GetOnSubprocessExitCallback(config));
  }
}

void LaunchSocketForwardProxyIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config) {
  if (AdbTunnelEnabled(config)) {
    cvd::Command adb_tunnel(config.socket_forward_proxy_binary());
    adb_tunnel.AddParameter(GetGuestPortArg());
    adb_tunnel.AddParameter(GetHostPortArg());
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
}

void LaunchSocketVsockProxyIfEnabled(cvd::ProcessMonitor* process_monitor,
                                 const vsoc::CuttlefishConfig& config) {
  if (AdbVsockTunnelEnabled(config)) {
    cvd::Command adb_tunnel(config.socket_vsock_proxy_binary());
    adb_tunnel.AddParameter("--vsock_port=6520");
    adb_tunnel.AddParameter(
        std::string{"--tcp_port="} + std::to_string(GetHostPort()));
    adb_tunnel.AddParameter(std::string{"--vsock_guest_cid="} +
                            std::to_string(config.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
  if (AdbVsockHalfTunnelEnabled(config)) {
    cvd::Command adb_tunnel(config.socket_vsock_proxy_binary());
    adb_tunnel.AddParameter("--vsock_port=5555");
    adb_tunnel.AddParameter(
        std::string{"--tcp_port="} + std::to_string(GetHostPort()));
    adb_tunnel.AddParameter(std::string{"--vsock_guest_cid="} +
                            std::to_string(config.vsock_guest_cid()));
    process_monitor->StartSubprocess(std::move(adb_tunnel),
                                     GetOnSubprocessExitCallback(config));
  }
}

void LaunchIvServerIfEnabled(cvd::ProcessMonitor* process_monitor,
                             const vsoc::CuttlefishConfig& config) {
  if (config.enable_ivserver()) {
    process_monitor->StartSubprocess(GetIvServerCommand(config),
                                     GetOnSubprocessExitCallback(config));

    // Initialize the regions that require so before the VM starts.
    PreLaunchInitializers::Initialize(config);
  }
}
