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
      "/bin/bash",      // TODO: b/359316164
      "/bin/mv",        // TODO: b/359314840
      "/usr/bin/lsof",  // TODO: b/359314623
                        // TODO: b/359309808
      "/usr/lib/cuttlefish-common/bin/capability_query.py",
      host.HostToolExe("avbtool"),                   // TODO: b/318610573
      host.HostToolExe("casimir"),                   // TODO: b/318613687
      host.HostToolExe("casimir_control_server"),    // TODO: b/318587667
      host.HostToolExe("control_env_proxy_server"),  // TODO: b/318592219
      host.HostToolExe("crosvm"),
      host.HostToolExe("echo_server"),             // TODO: b/318592223
      host.HostToolExe("extract-ikconfig"),        // TODO: b/359309462
      host.HostToolExe("gnss_grpc_proxy"),         // TODO: b/318591948
      host.HostToolExe("metrics"),                 // TODO: b/318594189
      host.HostToolExe("mkenvimage_slim"),         // TODO: b/318610408
      host.HostToolExe("netsimd"),                 // TODO: b/318603863
      host.HostToolExe("newfs_msdos"),             // TODO: b/318611835
      host.HostToolExe("openwrt_control_server"),  // TODO: b/318605411
      host.HostToolExe("operator_proxy"),          // TODO: b/359312147
      host.HostToolExe("root-canal"),              // TODO: b/359312761
      host.HostToolExe("simg2img"),                // TODO: b/359312017
      host.HostToolExe("tombstone_receiver"),      // TODO: b/318609742
      host.HostToolExe("vhost_device_vsock"),      // TODO: b/318613691
      host.HostToolExe("webrtc_operator"),         // TODO: b/359312626
      host.HostToolExe("wmediumd"),                // TODO: b//318610207
      host.HostToolExe("wmediumd_gen_config"),     // TODO: b/359313561
  };
}

}  // namespace cuttlefish::process_sandboxer
