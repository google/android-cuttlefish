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

#include <absl/strings/str_cat.h>
#include <absl/strings/str_replace.h>
#include <sandboxed_api/sandbox2/allow_all_syscalls.h>
#include <sandboxed_api/sandbox2/policybuilder.h>
#include <sandboxed_api/util/path.h>

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder RunCvdPolicy(const HostInfo& host) {
  std::string sandboxer_proxy = host.HostToolExe("sandboxer_proxy");
  return BaselinePolicy(host, host.HostToolExe("run_cvd"))
      .AddDirectory(host.runtime_dir, /* is_ro= */ false)
      .AddFile(host.cuttlefish_config_path)
      .AddFileAt(sandboxer_proxy,
                 "/usr/lib/cuttlefish-common/bin/capability_query.py")
      .AddFileAt(sandboxer_proxy, host.HostToolExe("adb_connector"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("casimir_control_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("control_env_proxy_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("crosvm"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("echo_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("gnss_grpc_proxy"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("kernel_log_monitor"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("log_tee"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("logcat_receiver"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("metrics"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("modem_simulator"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("netsimd"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("openwrt_control_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("operator_proxy"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("process_restarter"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("screen_recording_server"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("secure_env"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("socket_vsock_proxy"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("tcp_connector"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("tombstone_receiver"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("webRTC"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("webrtc_operator"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("wmediumd"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("wmediumd_gen_config"))
      .AddDirectory(host.environments_dir)
      .AddDirectory(host.environments_uds_dir, false)
      .AddDirectory(host.instance_uds_dir, false)
      // The UID inside the sandbox2 namespaces is always 1000.
      .AddDirectoryAt(host.environments_uds_dir,
                      absl::StrReplaceAll(
                          host.environments_uds_dir,
                          {{absl::StrCat("cf_env_", getuid()), "cf_env_1000"}}),
                      false)
      .AddDirectoryAt(host.instance_uds_dir,
                      absl::StrReplaceAll(
                          host.instance_uds_dir,
                          {{absl::StrCat("cf_avd_", getuid()), "cf_avd_1000"}}),
                      false)
      // TODO(schuffelen): Write a system call policy. As written, this only
      // uses the namespacing features of sandbox2, ignoring the seccomp
      // features.
      .DefaultAction(sandbox2::AllowAllSyscalls{});
}

}  // namespace cuttlefish::process_sandboxer
