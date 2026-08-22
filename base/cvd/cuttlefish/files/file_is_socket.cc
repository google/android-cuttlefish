/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "cuttlefish/files/file_is_socket.h"

#include <sys/stat.h>

#include <string>

#include "cuttlefish/posix/stat.h"

namespace cuttlefish {

bool FileIsSocket(const std::string& path) {
  static auto sock = [](const struct stat& st) { return S_ISSOCK(st.st_mode); };
  return Stat(path).transform(sock).value_or(false);
}

}  // namespace cuttlefish
