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

#include "cuttlefish/io/in_memory_fs.h"

#include <stdint.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cuttlefish/io/in_memory_io.h"
#include "cuttlefish/io/io.h"
#include "cuttlefish/io/reader_seeker.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::unique_ptr<ReaderSeeker>> InMemoryFilesystem::OpenReadOnly(
    std::string_view path) {
  return CF_EXPECT(OpenReadWrite(path));
}

Result<uint32_t> InMemoryFilesystem::FileAttributes(
    std::string_view path) const {
  return 0644;
}

Result<std::unique_ptr<ReaderWriterSeeker>> InMemoryFilesystem::CreateFile(
    std::string_view path) {
  std::lock_guard lock(mutex_);
  auto [it, inserted] =
      files_.try_emplace(std::string(path), std::vector<char>{});
  CF_EXPECTF(!!inserted, "'{}' already exists", path);
  return std::make_unique<class InMemoryIo>(it->second, mutex_);
}

Result<void> InMemoryFilesystem::DeleteFile(std::string_view path) {
  std::lock_guard lock(mutex_);
  auto it = files_.find(path);
  CF_EXPECTF(it != files_.end(), "No such file '{}'", path);
  files_.erase(it);
  return {};
}

Result<std::unique_ptr<ReaderWriterSeeker>> InMemoryFilesystem::OpenReadWrite(
    std::string_view path) {
  std::lock_guard lock(mutex_);
  auto it = files_.find(path);
  CF_EXPECTF(it != files_.end(), "'{}' does not exist", path);
  return std::make_unique<class InMemoryIo>(it->second, mutex_);
}

}  // namespace cuttlefish
