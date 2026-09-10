//
// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/posix/open_dir.h"

#include <dirent.h>
#include <errno.h>

#include <memory>
#include <string>
#include <string_view>

#include "cuttlefish/posix/strerror.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

void CloseDir::operator()(DIR* dir) {
  if (dir) {
    closedir(dir);
  }
}

Result<std::unique_ptr<DIR, CloseDir>> OpenDir(const char* path) {
  std::unique_ptr<DIR, CloseDir> ret(opendir(path));
  CF_EXPECTF(ret.get(), "opendir('{}') failed: {}'", path, StrError(errno));
  return ret;
}

Result<std::unique_ptr<DIR, CloseDir>> OpenDir(const std::string& path) {
  return CF_EXPECT(OpenDir(path.c_str()));
}

Result<std::unique_ptr<DIR, CloseDir>> OpenDir(std::string_view path) {
  return CF_EXPECT(OpenDir(std::string(path)));
}

}  // namespace cuttlefish
