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

#include <sandboxed_api/sandbox2/allow_all_syscalls.h>
#include <sandboxed_api/sandbox2/allow_unrestricted_networking.h>
#include <sandboxed_api/sandbox2/policybuilder.h>
#include <sandboxed_api/util/path.h>

namespace cuttlefish::process_sandboxer {

using sapi::file::JoinPath;

sandbox2::PolicyBuilder NetsimdPolicy(const HostInfo& host) {
  // TODO: b/318603863 - Add system call policy. This only applies namespaces.
  return BaselinePolicy(host, host.HostToolExe("netsimd"))
      .AddDirectory(JoinPath(host.host_artifacts_path, "bin", "netsim-ui"))
      .AddDirectory("/tmp", /* is_ro= */ false)  // to create new directories
      .AddDirectory(JoinPath(host.runtime_dir, "internal"), /* is_ro= */ false)
      .AddFile("/dev/urandom")  // For gRPC
      .Allow(sandbox2::UnrestrictedNetworking())
      .DefaultAction(sandbox2::AllowAllSyscalls());
}

}  // namespace cuttlefish::process_sandboxer
