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
#ifndef ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_SANDBOX_PROCESS_POLICIES_H
#define ANDROID_DEVICE_GOOGLE_CUTTLEFISH_HOST_COMMANDS_SANDBOX_PROCESS_POLICIES_H

#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>

#include "sandboxed_api/sandbox2/policybuilder.h"

namespace cuttlefish::process_sandboxer {

struct HostInfo {
  std::string HostToolExe(std::string_view exe) const;

  std::string assembly_dir;
  std::string cuttlefish_config_path;
  std::string environments_dir;
  std::string environments_uds_dir;
  std::string guest_image_path;
  std::string host_artifacts_path;
  std::string instance_uds_dir;
  std::string log_dir;
  std::string runtime_dir;
};

std::ostream& operator<<(std::ostream&, const HostInfo&);

sandbox2::PolicyBuilder BaselinePolicy(const HostInfo&, std::string_view exe);

sandbox2::PolicyBuilder AdbConnectorPolicy(const HostInfo&);
sandbox2::PolicyBuilder AssembleCvdPolicy(const HostInfo&);
sandbox2::PolicyBuilder EchoServerPolicy(const HostInfo&);
sandbox2::PolicyBuilder KernelLogMonitorPolicy(const HostInfo&);
sandbox2::PolicyBuilder LogTeePolicy(const HostInfo&);
sandbox2::PolicyBuilder LogcatReceiverPolicy(const HostInfo&);
sandbox2::PolicyBuilder ModemSimulatorPolicy(const HostInfo&);
sandbox2::PolicyBuilder ProcessRestarterPolicy(const HostInfo&);
sandbox2::PolicyBuilder RunCvdPolicy(const HostInfo&);
sandbox2::PolicyBuilder ScreenRecordingServerPolicy(const HostInfo&);
sandbox2::PolicyBuilder SecureEnvPolicy(const HostInfo&);
sandbox2::PolicyBuilder SocketVsockProxyPolicy(const HostInfo&);
sandbox2::PolicyBuilder TcpConnectorPolicy(const HostInfo&);
sandbox2::PolicyBuilder TombstoneReceiverPolicy(const HostInfo&);
sandbox2::PolicyBuilder WebRtcPolicy(const HostInfo&);

std::set<std::string> NoPolicy(const HostInfo&);

std::unique_ptr<sandbox2::Policy> PolicyForExecutable(
    const HostInfo& host_info, std::string_view server_socket_outside_path,
    std::string_view executable_path);

}  // namespace cuttlefish::process_sandboxer

#endif
