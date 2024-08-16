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

#include <set>
#include <string>

namespace cuttlefish::process_sandboxer {

// TODO(schuffelen): Reduce this list down to only `crosvm`
// Note that executables launched by executables listed here won't be tracked at
// all.
std::set<std::string> NoPolicy(const HostInfo& host) {
  return {
      "/usr/bin/lsof",  // TODO: b/359314623
                        // TODO: b/359309808
      "/usr/lib/cuttlefish-common/bin/capability_query.py",
      host.HostToolExe("avbtool"),                   // TODO: b/318610573
      host.HostToolExe("casimir"),                   // TODO: b/318613687
      host.HostToolExe("control_env_proxy_server"),  // TODO: b/318592219
      host.HostToolExe("crosvm"),
      host.HostToolExe("extract-ikconfig"),    // TODO: b/359309462
      host.HostToolExe("metrics"),             // TODO: b/318594189
      host.HostToolExe("root-canal"),          // TODO: b/359312761
      host.HostToolExe("vhost_device_vsock"),  // TODO: b/318613691
      host.HostToolExe("webrtc_operator"),     // TODO: b/359312626
  };
}

}  // namespace cuttlefish::process_sandboxer
