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
#include "cuttlefish/flag_parser/shared_fd_flag.h"

#include <unistd.h>

#include <string>
#include <string_view>

#include "absl/strings/numbers.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/flag_parser/flag.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Flag SharedFDFlag(const std::string& name, SharedFD& out) {
  return Flag::StringFlag(name).Setter(
      [&out, name](std::string_view arg) -> Result<void> {
        int raw_fd;
        CF_EXPECTF(absl::SimpleAtoi(arg, &raw_fd),
                   "Failed to parse value \"{}\" for fd flag \"--{}\"", arg,
                   name);
        out = SharedFD::Dup(raw_fd);
        CF_EXPECTF(out->IsOpen(), "Unable to dup file descriptor '{}': {}",
                   raw_fd, out->StrError());
        close(raw_fd);
        return {};
      });
}

}  // namespace cuttlefish
