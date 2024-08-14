/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/commands/process_sandboxer/policies.h"

#include <memory>
#include <ostream>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/util/path.h"

#include "host/commands/process_sandboxer/proxy_common.h"

using sapi::file::JoinPath;

namespace cuttlefish::process_sandboxer {

std::string HostInfo::HostToolExe(std::string_view exe) const {
  return JoinPath(host_artifacts_path, "bin", exe);
}

std::ostream& operator<<(std::ostream& out, const HostInfo& host) {
  out << "HostInfo {\n";
  out << "\tassembly_dir: \"" << host.assembly_dir << "\"\n";
  out << "\tcuttlefish_config_path: \"" << host.cuttlefish_config_path
      << "\"\n";
  out << "\tenvironments_dir: \"" << host.environments_dir << "\"\n";
  out << "\tenvironments_uds_dir: " << host.environments_uds_dir << "\"\n";
  out << "\tguest_image_path: " << host.guest_image_path << "\t\n";
  out << "\thost_artifacts_path: \"" << host.host_artifacts_path << "\"\n";
  out << "\tinstance_uds_dir: " << host.instance_uds_dir << "\"\n";
  out << "\tlog_dir: " << host.log_dir << "\"\n";
  out << "\truntime_dir: " << host.runtime_dir << "\"\n";
  return out << "}";
}

std::unique_ptr<sandbox2::Policy> PolicyForExecutable(
    const HostInfo& host, std::string_view server_socket_outside_path,
    std::string_view executable) {
  using Builder = sandbox2::PolicyBuilder(const HostInfo&);
  absl::flat_hash_map<std::string, Builder*> builders;

  builders[host.HostToolExe("adb_connector")] = AdbConnectorPolicy;
  builders[host.HostToolExe("assemble_cvd")] = AssembleCvdPolicy;
  builders[host.HostToolExe("echo_server")] = EchoServerPolicy;
  builders[host.HostToolExe("gnss_grpc_proxy")] = GnssGrpcProxyPolicy;
  builders[host.HostToolExe("kernel_log_monitor")] = KernelLogMonitorPolicy;
  builders[host.HostToolExe("log_tee")] = LogTeePolicy;
  builders[host.HostToolExe("logcat_receiver")] = LogcatReceiverPolicy;
  builders[host.HostToolExe("modem_simulator")] = ModemSimulatorPolicy;
  builders[host.HostToolExe("process_restarter")] = ProcessRestarterPolicy;
  builders[host.HostToolExe("run_cvd")] = RunCvdPolicy;
  builders[host.HostToolExe("screen_recording_server")] =
      ScreenRecordingServerPolicy;
  builders[host.HostToolExe("secure_env")] = SecureEnvPolicy;
  builders[host.HostToolExe("socket_vsock_proxy")] = SocketVsockProxyPolicy;
  builders[host.HostToolExe("tcp_connector")] = TcpConnectorPolicy;
  builders[host.HostToolExe("tombstone_receiver")] = TombstoneReceiverPolicy;
  builders[host.HostToolExe("webRTC")] = WebRtcPolicy;
  builders[host.HostToolExe("wmediumd")] = WmediumdPolicy;

  std::set<std::string> no_policy_set = NoPolicy(host);
  for (const auto& [exe, policy_builder] : builders) {
    if (no_policy_set.count(exe)) {
      LOG(FATAL) << "Overlap in policy map and no-policy set: '" << exe << "'";
    }
  }

  if (auto it = builders.find(executable); it != builders.end()) {
    // TODO(schuffelen): Only share this with executables known to launch others
    return (it->second)(host)
        .AddFileAt(server_socket_outside_path, kManagerSocketPath, false)
        .BuildOrDie();
  } else if (no_policy_set.count(std::string(executable))) {
    return nullptr;
  } else {
    LOG(FATAL) << "Unknown executable '" << executable << "'";
  }
}

}  // namespace cuttlefish::process_sandboxer
