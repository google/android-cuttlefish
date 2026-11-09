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

#include "cuttlefish/files/write_new_file.h"

#include <fcntl.h>
#include <sys/stat.h>

#include <string>

#include "absl/strings/string_view.h"

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/io/write_exact.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<void> WriteNewFile(const std::string& filepath,
                          absl::string_view content, mode_t mode) {
  Fd fd = CF_EXPECT(Fd::Open(filepath, O_CREAT | O_EXCL | O_WRONLY, mode));
  CF_EXPECT(WriteExact(fd, content));
  return {};
}

}  // namespace cuttlefish
