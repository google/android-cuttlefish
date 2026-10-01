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

#include "cuttlefish/host/commands/run_cvd/wattson_rebalance_threads.h"

#include <string>

#include "cuttlefish/files/directory_contents.h"
#include "cuttlefish/host/commands/run_cvd/move_threads_to_cgroup.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

// See go/vcpuinheritance for more context on why this Rebalance is
// required and what the stop gap/longterm solutions are.
Result<void> WattsonRebalanceThreads(const std::string& id) {
  auto root_path = "/sys/fs/cgroup/vsoc-" + id + "-cf";
  const auto files = CF_EXPECT(DirectoryContents(root_path));

  CF_EXPECT(MoveThreadsToCgroup(root_path, root_path + "/workers"));

  for (const auto& filename : files) {
    if (filename.find("vcpu-domain") != std::string::npos) {
      CF_EXPECT(MoveThreadsToCgroup(root_path + "/" + filename,
                                    root_path + "/workers"));
    }
  }
  return {};
}

}  // namespace cuttlefish
