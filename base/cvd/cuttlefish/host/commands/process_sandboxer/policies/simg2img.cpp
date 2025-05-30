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

#include <linux/filter.h>
#include <sys/mman.h>
#include <syscall.h>

#include <vector>

#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

namespace cuttlefish::process_sandboxer {

sandbox2::PolicyBuilder Simg2ImgPolicy(const HostInfo& host) {
  return BaselinePolicy(host, host.HostToolExe("simg2img"))
      .AddDirectory(host.guest_image_path, /* is_ro= */ false)
      .AddPolicyOnMmap([](bpf_labels& labels) -> std::vector<sock_filter> {
        return {
            ARG_32(2),  // prot
            JNE32(PROT_READ, JUMP(&labels, cf_simg2img_mmap_end)),
            ARG_32(3),  // flags
            JEQ32(MAP_SHARED, ALLOW),
            LABEL(&labels, cf_simg2img_mmap_end),
        };
      })
      .AllowSyscall(__NR_ftruncate);
}

}  // namespace cuttlefish::process_sandboxer
