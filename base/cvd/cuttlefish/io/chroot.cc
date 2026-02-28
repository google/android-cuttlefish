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

#include "cuttlefish/io/chroot.h"

#include <stdint.h>

#include <list>
#include <string>
#include <string_view>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/io/filesystem.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

ChrootReadWriteFilesystem::ChrootReadWriteFilesystem(
    ReadWriteFilesystem& real_filesystem, std::string_view path_prefix)
    : real_filesystem_(&real_filesystem), path_prefix_(path_prefix) {}

Result<std::unique_ptr<ReaderSeeker>> ChrootReadWriteFilesystem::OpenReadOnly(
    std::string_view path) {
  std::string real_path = CF_EXPECT(ChrootToRealPath(path));
  return CF_EXPECTF(real_filesystem_->OpenReadOnly(real_path),
                    "Failed for '{}' (actually '{}')", path, real_path);
}

Result<uint32_t> ChrootReadWriteFilesystem::FileAttributes(
    std::string_view path) const {
  std::string real_path = CF_EXPECT(ChrootToRealPath(path));
  return CF_EXPECTF(real_filesystem_->FileAttributes(real_path),
                    "Failed for '{}' (actually '{}')", path, real_path);
}

Result<std::unique_ptr<ReaderWriterSeeker>>
ChrootReadWriteFilesystem::CreateFile(std::string_view path) {
  std::string real_path = CF_EXPECT(ChrootToRealPath(path));
  return CF_EXPECTF(real_filesystem_->CreateFile(real_path),
                    "Failed for '{}' (actually '{}')", path, real_path);
}

Result<void> ChrootReadWriteFilesystem::DeleteFile(std::string_view path) {
  std::string real_path = CF_EXPECT(ChrootToRealPath(path));
  CF_EXPECTF(real_filesystem_->DeleteFile(real_path),
             "Failed for '{}' (actually '{}')", path, real_path);
  return {};
}

Result<std::unique_ptr<ReaderWriterSeeker>>
ChrootReadWriteFilesystem::OpenReadWrite(std::string_view path) {
  std::string real_path = CF_EXPECT(ChrootToRealPath(path));
  return CF_EXPECTF(real_filesystem_->OpenReadWrite(real_path),
                    "Failed for '{}' (actually '{}')", path, real_path);
}

Result<std::string> ChrootReadWriteFilesystem::ChrootToRealPath(
    std::string_view path) const {
  CF_EXPECTF(absl::StartsWith(path, "/"), "'{}' is not absolute", path);
  std::list<std::string_view> members =
      absl::StrSplit(path, '/', absl::SkipEmpty());
  for (auto it = members.begin(); it != members.end();) {
    if (*it == ".") {
      it = members.erase(it);
    } else if (*it == "..") {
      if (it != members.begin()) {
        it--;
        it = members.erase(it);
      }
      it = members.erase(it);
    } else {
      it++;
    }
  }
  return absl::StrCat(path_prefix_, "/", absl::StrJoin(members, "/"));
}

}  // namespace cuttlefish
