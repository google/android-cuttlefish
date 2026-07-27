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

#include "cuttlefish/files/link_or_copy.h"

#include <errno.h>
#include <unistd.h>

#include <string>

#include "absl/log/log.h"

#include "cuttlefish/files/are_hard_linked.h"
#include "cuttlefish/files/copy.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::string> LinkOrCopy(const std::string& target,
                               const std::string& destination,
                               const bool overwrite_existing) {
  if (FileExists(destination)) {
    if (CF_EXPECT(AreHardLinked(target, destination))) {
      return destination;
    }
    if (!overwrite_existing) {
      return CF_ERRF(
          "Cannot link/copy from \"{}\" to \"{}\", the second file already "
          "exists and is not a hard link to the first",
          target, destination);
    }
    LOG(WARNING) << "Overwriting existing file \"" << destination
                 << "\" with \"" << target << "\" from the cache";
    CF_EXPECTF(unlink(destination.c_str()) == 0,
               "Failed to unlink \"{}\" with error: {}", destination,
               StrError(errno));
  }
  if (link(target.c_str(), destination.c_str()) == 0) {
    VLOG(1) << "Created hard link from \"" << target << "\" to \""
            << destination << "\"";
    return destination;
  }
  CF_EXPECTF(Copy(target, destination), "Failed to copy \"{}\" to \"{}\"",
             target, destination);
  VLOG(1) << "Copied file from \"" << target << "\" to \"" << destination
          << "\"";

  return destination;
}

}  // namespace cuttlefish
