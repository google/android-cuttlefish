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

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/util/path.h"

using sapi::file::JoinPath;

namespace cuttlefish {
namespace process_sandboxer {

sandbox2::PolicyBuilder BaselinePolicy(const HostInfo& host,
                                       std::string_view exe) {
  return sandbox2::PolicyBuilder()
      .AddLibrariesForBinary(exe, JoinPath(host.artifacts_path, "lib64"))
      // For dynamic linking and memory allocation
      .AllowDynamicStartup()
      .AllowExit()
      .AllowGetPIDs()
      .AllowGetRandom()
      .AllowMmap()
      .AllowReadlink()
      .AllowRestartableSequences(sandbox2::PolicyBuilder::kAllowSlowFences)
      .AllowWrite();
}

}  // namespace process_sandboxer
}  // namespace cuttlefish
