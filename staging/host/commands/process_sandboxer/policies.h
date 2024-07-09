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

#include <string>
#include <string_view>

#include "sandboxed_api/sandbox2/policybuilder.h"

namespace cuttlefish {
namespace process_sandboxer {

struct HostInfo {
  std::string artifacts_path;
  std::string cuttlefish_config_path;
  std::string log_dir;
};

sandbox2::PolicyBuilder BaselinePolicy(const HostInfo&, std::string_view exe);

sandbox2::PolicyBuilder KernelLogMonitorPolicy(const HostInfo&);
sandbox2::PolicyBuilder LogcatReceiverPolicy(const HostInfo&);

// Testing policies
sandbox2::PolicyBuilder HelloWorldPolicy(const HostInfo&);

std::unique_ptr<sandbox2::Policy> PolicyForExecutable(
    const HostInfo& host_info, std::string_view executable_path);

}  // namespace process_sandboxer
}  // namespace cuttlefish

#endif
