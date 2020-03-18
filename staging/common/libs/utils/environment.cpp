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

#include "common/libs/utils/environment.h"

#include <stdlib.h>
#include <stdio.h>
#include <iostream>

namespace cvd {

std::string StringFromEnv(const std::string& varname,
                          const std::string& defval) {
  const char* const valstr = getenv(varname.c_str());
  if (!valstr) {
    return defval;
  }
  return valstr;
}

/**
 * at runtime, return the arch of the host: e.g. aarch64, x86_64, etc
 *
 * uses "`which uname` -m"
 *
 * @return arch string on success, "" on failure
 */
std::string HostArch() {
  static std::string arch;
  static bool cached = false;

  if (cached) {
    return arch;
  }
  cached = true;

  // good to check if uname exists and is executable
  // or, guarantee uname is availabe by dependency list
  FILE* pip = popen("uname -m", "r");
  if (!pip) {
    return std::string{};
  }

  auto read_from_file =
      [](FILE* fp, size_t len) {
        /*
         * to see if input is longer than len,
         * we read up to len+1. If the length is len+1,
         * then the input is too long
         */
        decltype(len) upper = len + 1;
        std::string format("%");
        format.append(std::to_string(upper)).append("s");
        std::shared_ptr<char> buf(new char[upper],
                                  std::default_delete<char[]>());
        if (fscanf(fp, format.c_str(), buf.get()) == EOF) {
          return std::string{};
        }
        std::string result(buf.get());
        return (result.length() < upper) ? result : std::string{};
      };
  arch = read_from_file(pip, 20);
  pclose(pip);

  // l and r trim on arch
  static const char* whitespace = "\t\n\r\f\v ";
  arch.erase(arch.find_last_not_of(whitespace) + 1); // r trim
  arch.erase(0, arch.find_first_not_of(whitespace)); // l trim
  return arch;
}

}  // namespace cvd
