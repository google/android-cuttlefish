/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/cvd/cli/commands/acloud_common.h"

#include <string>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/files.h"
#include "cuttlefish/host/commands/cvd/legacy/cvd_server.pb.h"

namespace cuttlefish {

bool IsSubOperationSupported(const CommandRequest& request) {
  if (request.SubcommandArguments().empty()) {
    return false;
  }
  return request.SubcommandArguments()[0] == "create";
}

Result<void> PrepareForAcloudDeleteCommand(
    const cvd::InstanceGroupInfo& group_info) {
  std::string host_path = group_info.host_artifacts_path();
  std::string stop_cvd_path = fmt::format("{}/bin/stop_cvd", host_path);
  std::string cvd_internal_stop_path =
      fmt::format("{}/bin/cvd_internal_stop", host_path);
  if (FileExists(cvd_internal_stop_path)) {
    // cvd_internal_stop exists, stop_cvd is just a symlink to it
    CF_EXPECT(RemoveFile(stop_cvd_path), "Failed to remove stop_cvd file");
  } else {
    // cvd_internal_stop doesn't exist, stop_cvd is the actual executable file
    CF_EXPECT(RenameFile(stop_cvd_path, cvd_internal_stop_path),
              "Failed to rename stop_cvd as cvd_internal_stop");
  }
  SharedFD stop_cvd_fd = SharedFD::Creat(stop_cvd_path, 0775);
  CF_EXPECTF(stop_cvd_fd->IsOpen(), "Failed to create stop_cvd executable: {}",
             stop_cvd_fd->StrError());
  // Don't include the group name in the rm command, it's not needed for a
  // single instance group and won't know which group needs to be removed if
  // multiple groups exist. Acloud delete will set the HOME variable, which
  // means cvd rm will pick the right group.
  std::string stop_cvd_content = "#!/bin/sh\ncvd rm";
  auto ret = WriteAll(stop_cvd_fd, stop_cvd_content);
  CF_EXPECT(ret == (ssize_t)stop_cvd_content.size(),
            "Failed to write to stop_cvd script");
  return {};
}

}  // namespace cuttlefish
