/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "common/libs/utils/architecture.h"

#include <sys/utsname.h>

#include <cstdlib>
#include <string>

#include <android-base/logging.h>
#include <android-base/no_destructor.h>
#include <android-base/strings.h>

namespace cuttlefish {

/** Returns e.g. aarch64, x86_64, etc */
const std::string& HostArchStr() {
  static android::base::NoDestructor<std::string> arch([] {
    utsname buf;
    CHECK_EQ(uname(&buf), 0) << strerror(errno);
    return std::string(buf.machine);
  }());
  return *arch;
}

Arch HostArch() {
  std::string arch_str = HostArchStr();
  if (arch_str == "aarch64" || arch_str == "arm64") {
    return Arch::Arm64;
  } else if (arch_str == "arm") {
    return Arch::Arm;
  } else if (arch_str == "riscv64") {
    return Arch::RiscV64;
  } else if (arch_str == "x86_64") {
    return Arch::X86_64;
  } else if (arch_str.size() == 4 && arch_str[0] == 'i' && arch_str[2] == '8' &&
             arch_str[3] == '6') {
    return Arch::X86;
  } else {
    LOG(FATAL) << "Unknown host architecture: " << arch_str;
    return Arch::X86;
  }
}

bool IsHostCompatible(Arch arch) {
  Arch host_arch = HostArch();
  return arch == host_arch || (arch == Arch::Arm && host_arch == Arch::Arm64) ||
         (arch == Arch::X86 && host_arch == Arch::X86_64);
}

}  // namespace cuttlefish
