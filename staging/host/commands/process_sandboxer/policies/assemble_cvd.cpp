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

sandbox2::PolicyBuilder AssembleCvdPolicy(const HostInfo& host) {
  std::string sandboxer_proxy = host.HostToolExe("sandboxer_proxy");
  return BaselinePolicy(host, host.HostToolExe("assemble_cvd"))
      .AddDirectory(host.assembly_dir, /* is_ro= */ false)
      // TODO(schuffelen): Don't resize vbmeta in-place
      .AddDirectory(host.guest_image_path, /* is_ro= */ false)
      .AddDirectory(JoinPath(host.host_artifacts_path, "etc", "cvd_config"))
      // TODO(schuffelen): Copy these files before modifying them
      .AddDirectory(JoinPath(host.host_artifacts_path, "etc", "openwrt"),
                    /* is_ro= */ false)
      // TODO(schuffelen): Premake the directory for extract-ikconfig outputs
      .AddDirectory("/tmp", /* is_ro= */ false)
      .AddDirectory(host.environments_dir, /* is_ro= */ false)
      .AddDirectory(host.environments_uds_dir, /* is_ro= */ false)
      .AddDirectory(host.instance_uds_dir, /* is_ro= */ false)
      .AddDirectory(host.runtime_dir, /* is_ro= */ false)
      .AddFileAt(sandboxer_proxy,
                 "/usr/lib/cuttlefish-common/bin/capability_query.py")
      .AddFileAt(sandboxer_proxy, "/bin/bash")
      .AddFileAt(sandboxer_proxy, host.HostToolExe("avbtool"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("crosvm"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("extract-ikconfig"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("mkenvimage_slim"))
      .AddFileAt(sandboxer_proxy, host.HostToolExe("newfs_msdos"))
      // TODO(schuffelen): Do this in-process?
      .AddFileAt(sandboxer_proxy, host.HostToolExe("simg2img"))
      .AddFileAt(sandboxer_proxy, "/usr/bin/lsof")
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
