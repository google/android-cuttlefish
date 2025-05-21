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

#include "cuttlefish/host/commands/process_sandboxer/policies.h"

#include <stdlib.h>

#include <cerrno>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"

#include "cuttlefish/host/commands/process_sandboxer/proxy_common.h"

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;
using sapi::file_util::fileops::CreateDirectoryRecursively;

absl::Status HostInfo::EnsureOutputDirectoriesExist() {
  if (!CreateDirectoryRecursively(assembly_dir, 0700)) {
    return absl::ErrnoToStatus(errno, "Failed to create " + assembly_dir);
  }
  if (!CreateDirectoryRecursively(environments_dir, 0700)) {
    return absl::ErrnoToStatus(errno, "Failed to create " + environments_dir);
  }
  if (!CreateDirectoryRecursively(EnvironmentsUdsDir(), 0700)) {
    return absl::ErrnoToStatus(errno,
                               "Failed to create " + EnvironmentsUdsDir());
  }
  if (!CreateDirectoryRecursively(InstanceUdsDir(), 0700)) {
    return absl::ErrnoToStatus(errno, "Failed to create " + InstanceUdsDir());
  }
  if (!CreateDirectoryRecursively(log_dir, 0700)) {
    return absl::ErrnoToStatus(errno, "Failed to create " + log_dir);
  }
  if (!CreateDirectoryRecursively(runtime_dir, 0700)) {
    return absl::ErrnoToStatus(errno, "Failed to create " + runtime_dir);
  }
  if (!CreateDirectoryRecursively(VsockDeviceDir(), 0700)) {
    return absl::ErrnoToStatus(errno, "Failed to create " + runtime_dir);
  }
  return absl::OkStatus();
}

std::string HostInfo::EnvironmentsUdsDir() const {
  return JoinPath(tmp_dir, "cf_env_1000");
}

std::string HostInfo::HostToolExe(std::string_view exe) const {
  return JoinPath(host_artifacts_path, "bin", exe);
}

std::string HostInfo::InstanceUdsDir() const {
  return JoinPath(tmp_dir, "cf_avd_1000/cvd-1");
}

std::string HostInfo::VsockDeviceDir() const {
  return JoinPath(tmp_dir, "vsock_3_1000");
}

std::ostream& operator<<(std::ostream& out, const HostInfo& host) {
  out << "HostInfo {\n";
  out << "\tassembly_dir: \"" << host.assembly_dir << "\"\n";
  out << "\tcuttlefish_config_path: \"" << host.cuttlefish_config_path
      << "\"\n";
  out << "\tenvironments_dir: \"" << host.environments_dir << "\"\n";
  out << "\tenvironments_uds_dir: " << host.EnvironmentsUdsDir() << "\"\n";
  out << "\tguest_image_path: " << host.guest_image_path << "\t\n";
  out << "\thost_artifacts_path: \"" << host.host_artifacts_path << "\"\n";
  out << "\tinstance_uds_dir: " << host.InstanceUdsDir() << "\"\n";
  out << "\tlog_dir: " << host.log_dir << "\"\n";
  out << "\truntime_dir: " << host.runtime_dir << "\"\n";
  out << "\ttmp_dir: \"" << host.tmp_dir << "\"\n";
  out << "\tvsock_device_dir: \"" << host.VsockDeviceDir() << "\"\n";
  return out << "}";
}

std::unique_ptr<sandbox2::Policy> PolicyForExecutable(
    const HostInfo& host, std::string_view server_socket_outside_path,
    std::string_view executable) {
  using Builder = sandbox2::PolicyBuilder(const HostInfo&);
  absl::flat_hash_map<std::string, Builder*> builders;

  builders[host.HostToolExe("adb_connector")] = AdbConnectorPolicy;
  builders[host.HostToolExe("assemble_cvd")] = AssembleCvdPolicy;
  builders[host.HostToolExe("avbtool")] = AvbToolPolicy;
  builders[host.HostToolExe("casimir")] = CasimirPolicy;
  builders[host.HostToolExe("cf_vhost_user_input")] = CfVhostUserInput;
  builders[host.HostToolExe("casimir_control_server")] =
      CasimirControlServerPolicy;
  builders[host.HostToolExe("control_env_proxy_server")] =
      ControlEnvProxyServerPolicy;
  builders[host.HostToolExe("cvd_internal_start")] = CvdInternalStartPolicy;
  builders[host.HostToolExe("echo_server")] = EchoServerPolicy;
  builders[host.HostToolExe("gnss_grpc_proxy")] = GnssGrpcProxyPolicy;
  builders[host.HostToolExe("kernel_log_monitor")] = KernelLogMonitorPolicy;
  builders[host.HostToolExe("log_tee")] = LogTeePolicy;
  builders[host.HostToolExe("logcat_receiver")] = LogcatReceiverPolicy;
  builders[host.HostToolExe("metrics")] = MetricsPolicy;
  builders[host.HostToolExe("mkenvimage_slim")] = MkEnvImgSlimPolicy;
  builders[host.HostToolExe("modem_simulator")] = ModemSimulatorPolicy;
  builders[host.HostToolExe("netsimd")] = NetsimdPolicy;
  builders[host.HostToolExe("newfs_msdos")] = NewFsMsDosPolicy;
  builders[host.HostToolExe("openwrt_control_server")] =
      OpenWrtControlServerPolicy;
  builders[host.HostToolExe("operator_proxy")] = OperatorProxyPolicy;
  builders[host.HostToolExe("process_restarter")] = ProcessRestarterPolicy;
  builders[host.HostToolExe("run_cvd")] = RunCvdPolicy;
  builders[host.HostToolExe("screen_recording_server")] =
      ScreenRecordingServerPolicy;
  builders[host.HostToolExe("secure_env")] = SecureEnvPolicy;
  builders[host.HostToolExe("simg2img")] = Simg2ImgPolicy;
  builders[host.HostToolExe("socket_vsock_proxy")] = SocketVsockProxyPolicy;
  builders[host.HostToolExe("tcp_connector")] = TcpConnectorPolicy;
  builders[host.HostToolExe("tombstone_receiver")] = TombstoneReceiverPolicy;
  builders[host.HostToolExe("vhost_device_vsock")] = VhostDeviceVsockPolicy;
  builders[host.HostToolExe("webRTC")] = WebRtcPolicy;
  builders[host.HostToolExe("webrtc_operator")] = WebRtcOperatorPolicy;
  builders[host.HostToolExe("wmediumd")] = WmediumdPolicy;
  builders[host.HostToolExe("wmediumd_gen_config")] = WmediumdGenConfigPolicy;

  std::set<std::string> no_policy_set = NoPolicy(host);
  for (const auto& [exe, policy_builder] : builders) {
    if (no_policy_set.count(exe)) {
      LOG(FATAL) << "Overlap in policy map and no-policy set: '" << exe << "'";
    }
  }

  if (auto it = builders.find(executable); it != builders.end()) {
    // TODO(schuffelen): Only share this with executables known to launch others
    auto r1 = (it->second)(host);
    r1.AddFileAt(server_socket_outside_path, kManagerSocketPath, false);
    auto r2 = r1.TryBuild();
    if (!r2.ok()) {
      LOG(INFO) << r2.status().ToString();
      abort();
    }
    return std::move(*r2);
  } else if (no_policy_set.count(std::string(executable))) {
    return nullptr;
  } else {
    LOG(FATAL) << "Unknown executable '" << executable << "'";
  }
}

}  // namespace cuttlefish::process_sandboxer
