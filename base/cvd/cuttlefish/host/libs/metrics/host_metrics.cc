/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/libs/metrics/host_metrics.h"

#include <sys/utsname.h>

#include <cstring>
#include <string>

#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {
namespace {

HostArch ToHostArch(std::string_view arch_str) {
  if (arch_str == "aarch64" || arch_str == "arm64") {
    return HostArch::Arm64;
  } else if (arch_str == "arm") {
    return HostArch::Arm;
  } else if (arch_str == "riscv64") {
    return HostArch::RiscV64;
  } else if (arch_str == "x86_64") {
    return HostArch::X86_64;
  } else if (arch_str.size() == 4 && arch_str[0] == 'i' && arch_str[2] == '8' &&
             arch_str[3] == '6') {
    return HostArch::X86;
  }
  return HostArch::Unknown;
}

HostOs ToHostOs(std::string_view os_str) {
  if (os_str == "GNU/Linux") {
    return HostOs::Linux;
  }
  return HostOs::Unknown;
}

}  // namespace

Result<HostMetrics> GetHostMetrics() {
  utsname out;
  CF_EXPECTF(uname(&out) == 0, "uname() call failed with error: {}",
             strerror(errno));
  return HostMetrics{
      .arch = ToHostArch(std::string(out.machine)),
      .os = ToHostOs(std::string(out.sysname)),
      .release = std::string(out.release),
  };
}

}  // namespace cuttlefish
