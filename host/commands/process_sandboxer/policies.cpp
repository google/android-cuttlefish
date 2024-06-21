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

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/util/path.h"

using sapi::file::JoinPath;

namespace cuttlefish {

std::unique_ptr<sandbox2::Policy> PolicyForExecutable(
    const HostInfo& host, std::string_view executable) {
  using Builder = sandbox2::PolicyBuilder(const HostInfo&);
  absl::flat_hash_map<std::string, Builder*> builders;

  builders[JoinPath(host.artifacts_path, "bin", "kernel_log_monitor")] =
      KernelLogMonitorPolicy;
  builders[JoinPath(host.artifacts_path, "bin", "logcat_receiver")] =
      LogcatReceiverPolicy;

  // TODO(schuffelen): Don't include test policies in the production impl
  builders[JoinPath(host.artifacts_path, "testcases", "process_sandboxer_test",
                    "x86_64", "process_sandboxer_test_hello_world")] =
      HelloWorldPolicy;

  if (auto it = builders.find(executable); it != builders.end()) {
    return (it->second)(host).BuildOrDie();
  } else {
    for (const auto& [target, unused] : builders) {
      LOG(ERROR) << "Available policy: '" << target << "'";
    }
    LOG(FATAL) << "No policy defined for '" << executable << "'";
    return sandbox2::PolicyBuilder().BuildOrDie();
  }
}

}  // namespace cuttlefish
