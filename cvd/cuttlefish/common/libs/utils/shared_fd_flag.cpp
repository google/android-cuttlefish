/*
 * Copyright (C) 2021 The Android Open Source Project
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
#include "common/libs/utils/shared_fd_flag.h"

#include <unistd.h>

#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <string>

#include <android-base/logging.h>
#include <android-base/parseint.h>

#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/flag_parser.h"

namespace cuttlefish {

static bool Set(const FlagMatch& match, SharedFD& out) {
  int raw_fd;
  if (!android::base::ParseInt(match.value.c_str(), &raw_fd)) {
    LOG(ERROR) << "Failed to parse value \"" << match.value
               << "\" for fd flag \"" << match.key << "\"";
    return false;
  }
  out = SharedFD::Dup(raw_fd);
  if (out->IsOpen()) {
    close(raw_fd);
  }
  return true;
}

Flag SharedFDFlag(SharedFD& out) {
  return Flag().Setter([&](const FlagMatch& mat) { return Set(mat, out); });
}
Flag SharedFDFlag(const std::string& name, SharedFD& out) {
  return GflagsCompatFlag(name).Setter(
      [&out](const FlagMatch& mat) { return Set(mat, out); });
}

}  // namespace cuttlefish
