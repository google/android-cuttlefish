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

#include <set>
#include <string>

namespace cuttlefish::process_sandboxer {

// TODO(schuffelen): Reduce this list down to only `crosvm`
// Note that executables launched by executables listed here won't be tracked at
// all.
std::set<std::string> NoPolicy(const HostInfo& host) {
  return {
      host.HostToolExe("crosvm"),
      "/usr/bin/openssl",  // TODO
  };
}

}  // namespace cuttlefish::process_sandboxer
