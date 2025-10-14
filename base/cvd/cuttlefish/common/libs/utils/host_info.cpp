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

#include "cuttlefish/common/libs/utils/host_info.h"

#include <sys/utsname.h>

#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>

#include <android-base/logging.h>
#include <android-base/no_destructor.h>
#include <android-base/strings.h>

namespace cuttlefish {
namespace {

struct HostUname {
  std::string arch;
  std::string os;
  std::string release;
};

HostUname GetHostUname() {
  utsname out;
  CHECK_EQ(uname(&out), 0) << strerror(errno);
  return HostUname{
      .arch = std::string(out.machine),
      .os = std::string(out.sysname),
      .release = std::string(out.release),
  };
}

Arch HostArch(std::string_view arch_str) {
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

Os HostOs(std::string_view os_str) {
  if (os_str == "GNU/Linux") {
    return Os::Linux;
  }
  return Os::Unknown;
}

}  // namespace

/** Returns e.g. aarch64, x86_64, etc */
const std::string& HostArchStr() {
  static android::base::NoDestructor<std::string> arch(
      [] { return GetHostUname().arch; }());
  return *arch;
}

Arch HostArch() { return HostArch(GetHostUname().arch); }

HostInfo GetHostInfo() {
  const HostUname host_uname = GetHostUname();
  return HostInfo{
      .arch = HostArch(host_uname.arch),
      .os = HostOs(host_uname.os),
      .release = host_uname.release,
  };
}

bool IsHostCompatible(Arch arch) {
  Arch host_arch = HostArch();
  return arch == host_arch || (arch == Arch::Arm && host_arch == Arch::Arm64) ||
         (arch == Arch::X86 && host_arch == Arch::X86_64);
}

std::ostream& operator<<(std::ostream& out, Arch arch) {
  switch (arch) {
    case Arch::Arm:
      return out << "arm";
    case Arch::Arm64:
      return out << "arm64";
    case Arch::RiscV64:
      return out << "riscv64";
    case Arch::X86:
      return out << "x86";
    case Arch::X86_64:
      return out << "x86_64";
  }
}

std::ostream& operator<<(std::ostream& out, Os arch) {
  switch (arch) {
    case Os::Linux:
      return out << "GNU/Linux";
    case Os::Unknown:
      return out << "unknown";
  }
}

}  // namespace cuttlefish
