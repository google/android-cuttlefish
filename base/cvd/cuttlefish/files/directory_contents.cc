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

#include "cuttlefish/files/directory_contents.h"

#include <dirent.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::vector<std::string>> DirectoryContents(const std::string& path) {
  std::vector<std::string> ret;
  std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(path.c_str()), closedir);
  CF_EXPECTF(dir != nullptr, "Could not read from dir \"{}\"", path);
  struct dirent* ent{};
  while ((ent = readdir(dir.get()))) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    ret.emplace_back(ent->d_name);
  }
  return ret;
}

}  // namespace cuttlefish
