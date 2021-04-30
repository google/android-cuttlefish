//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/run_cvd/launch.h"

#include <android-base/logging.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/run_cvd/process_monitor.h"
#include "host/commands/run_cvd/runner_defs.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/config/known_paths.h"

namespace cuttlefish {

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
    command.AddParameter("-subscriber_fds=");
    for (unsigned int i = 0; i < number_of_event_pipes; ++i) {
      SharedFD event_pipe_write_end, event_pipe_read_end;
      if (!SharedFD::Pipe(&event_pipe_read_end, &event_pipe_write_end)) {
        LOG(ERROR) << "Unable to create kernel log events pipe: " << strerror(errno);
        std::exit(RunnerExitCodes::kPipeIOError);
      }
      if (i > 0) {
        command.AppendToLastParameter(",");
      }
      command.AppendToLastParameter(event_pipe_write_end);
      ret.push_back(event_pipe_read_end);
    }
  }

  process_monitor->AddCommand(std::move(command));

  return ret;
}

void LaunchRootCanal(const CuttlefishConfig& config,
                     ProcessMonitor* process_monitor) {
  if (!config.enable_host_bluetooth()) {
    return;
  }

  auto instance = config.ForDefaultInstance();
  Command command(RootCanalBinary());

  // Test port
  command.AddParameter(instance.rootcanal_test_port());
  // HCI server port
  command.AddParameter(instance.rootcanal_hci_port());
  // Link server port
  command.AddParameter(instance.rootcanal_link_port());
  // Bluetooth controller properties file
  command.AddParameter("--controller_properties_file=",
                       instance.rootcanal_config_file());
  // Default commands file
  command.AddParameter("--default_commands_file=",
                       instance.rootcanal_default_commands_file());

  process_monitor->AddCommand(std::move(command));
  return;
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

  process_monitor->AddCommand(std::move(command));
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
  process_monitor->AddCommand(std::move(cmd));
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

  process_monitor->AddCommand(std::move(cmd));
  return;
}

void LaunchMetrics(ProcessMonitor* process_monitor) {
  Command metrics(MetricsBinary());

  process_monitor->AddCommand(std::move(metrics));
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
    if (!instance.gnss_file_path().empty()) {
      // If path is provided, proxy will start as local mode.
      gnss_grpc_proxy_cmd.AddParameter("--gnss_file_path=", instance.gnss_file_path());
    }
    process_monitor->AddCommand(std::move(gnss_grpc_proxy_cmd));
}

void LaunchBluetoothConnector(ProcessMonitor* process_monitor,
                              const CuttlefishConfig& config) {
  auto instance = config.ForDefaultInstance();
  std::vector<std::string> fifo_paths = {
      instance.PerInstanceInternalPath("bt_fifo_vm.in"),
      instance.PerInstanceInternalPath("bt_fifo_vm.out"),
  };
  std::vector<SharedFD> fifos;
  for (const auto& path : fifo_paths) {
    unlink(path.c_str());
    if (mkfifo(path.c_str(), 0660) < 0) {
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

  Command command(DefaultHostArtifactsPath("bin/bt_connector"));
  command.AddParameter("-bt_out=", fifos[0]);
  command.AddParameter("-bt_in=", fifos[1]);
  command.AddParameter("-hci_port=", instance.rootcanal_hci_port());
  command.AddParameter("-link_port=", instance.rootcanal_link_port());
  command.AddParameter("-test_port=", instance.rootcanal_test_port());
  process_monitor->AddCommand(std::move(command));
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

  Command command(HostBinaryPath("secure_env"));
  command.AddParameter("-keymaster_fd_out=", fifos[0]);
  command.AddParameter("-keymaster_fd_in=", fifos[1]);
  command.AddParameter("-gatekeeper_fd_out=", fifos[2]);
  command.AddParameter("-gatekeeper_fd_in=", fifos[3]);

  const auto& secure_hals = config.secure_hals();
  bool secure_keymint = secure_hals.count(SecureHal::Keymint) > 0;
  command.AddParameter("-keymint_impl=", secure_keymint ? "tpm" : "software");
  bool secure_gatekeeper = secure_hals.count(SecureHal::Gatekeeper) > 0;
  auto gatekeeper_impl = secure_gatekeeper ? "tpm" : "software";
  command.AddParameter("-gatekeeper_impl=", gatekeeper_impl);

  process_monitor->AddCommand(std::move(command));
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
      process_monitor->AddCommand(std::move(command));

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
  process_monitor->AddCommand(std::move(grpc_server));
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
  process_monitor->AddCommand(std::move(console_forwarder_cmd));
}

} // namespace cuttlefish
