/*
 * Copyright (C) 2026 The Android Open Source Project
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
#include "cuttlefish/host/libs/config/log_string_to_dir.h"

#include <fcntl.h>
#include <stddef.h>

#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/host/libs/config/cuttlefish_config.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<void> LogStringToDir(const CuttlefishConfig::InstanceSpecific& instance,
                            std::string_view name, std::string_view contents) {
  std::string file = instance.PerInstanceLogPath(name);

  Result<SharedFD> fd_res = Fd::Open(file, O_CREAT | O_EXCL | O_RDWR, 0644);
  for (size_t counter = 1; !fd_res.has_value(); counter++) {
    CF_EXPECT_LT(counter, 100);

    file = instance.PerInstanceLogPath(absl::StrCat(name, ".", counter));
    fd_res = Fd::Open(file, O_CREAT | O_EXCL | O_RDWR, 0644);
  }

  SharedFD fd = CF_EXPECT(std::move(fd_res));
  CF_EXPECT_EQ(WriteAll(fd, contents), contents.size(), fd->StrError());

  return {};
}

}  // namespace cuttlefish
