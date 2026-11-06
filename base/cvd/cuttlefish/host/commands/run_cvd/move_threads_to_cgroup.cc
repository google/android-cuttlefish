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

#include "cuttlefish/host/commands/run_cvd/move_threads_to_cgroup.h"

#include <fcntl.h>

#include <sstream>
#include <string>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> MoveThreadsToCgroup(const std::string& from_path,
                                 const std::string& to_path) {
  std::string file_path = from_path + "/cgroup.threads";

  if (FileExists(file_path)) {
    Result<std::string> content_result = ReadFileContents(file_path);
    if (!content_result.has_value()) {
      LOG(INFO) << "Failed to open threads file and assume it is empty: "
                << file_path;
      return {};
    }

    std::istringstream is(content_result.value());
    std::string each_id;
    while (std::getline(is, each_id)) {
      std::string proc_status_path = "/proc/" + each_id;
      proc_status_path.append("/status");
      Result<std::string> proc_status = ReadFileContents(proc_status_path);
      if (!proc_status.has_value()) {
        LOG(INFO) << "Failed to open proc status file and skip: "
                  << proc_status_path;
        continue;
      }

      std::string proc_status_str = proc_status.value();
      if (proc_status_str.find("crosvm_vcpu") == std::string::npos &&
          proc_status_str.find("vcpu_throttle") == std::string::npos) {
        // other proc moved to workers cgroup
        std::string to_path_file = to_path + "/cgroup.threads";
        Fd fd = CF_EXPECT(Fd::Open(to_path_file, O_WRONLY | O_APPEND));
        CF_EXPECTF(WriteExact(fd, each_id), "Failed to write to '{}'",
                   to_path_file);
      }
    }
  }

  return {};
}

}  // namespace cuttlefish
