/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/metrics/io/unique_id_file.h"

#include <string>

#include "fmt/format.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/uuid.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/directories/xdg.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kSessionIdFileName[] = "metrics_session_id.txt";
constexpr char kUserIdFileName[] = "metrics_user_id.txt";

Result<std::string> ReadUniqueIdFile(const std::string& directory,
                                     const std::string& filename) {
  return CF_EXPECT(ReadFileContents(fmt::format("{}/{}", directory, filename)));
}

Result<void> GenerateUniqueIdFile(const std::string& directory,
                                  const std::string& filename) {
  const std::string filepath = fmt::format("{}/{}", directory, filename);
  if (FileExists(filepath)) {
    // a unique ID file might already exist from previous runs or
    // shared directories between fetching and device creation
    return {};
  }
  const std::string unique_id = GenerateUuid();
  CF_EXPECT(EnsureDirectoryExists(directory));
  CF_EXPECT(WriteNewFile(filepath, unique_id));
  return {};
}

}  // namespace

Result<std::string> ReadSessionIdFile(const std::string& metrics_directory) {
  return CF_EXPECT(ReadUniqueIdFile(metrics_directory, kSessionIdFileName));
}

Result<void> GenerateSessionIdFile(const std::string& metrics_directory) {
  CF_EXPECT(GenerateUniqueIdFile(metrics_directory, kSessionIdFileName));
  return {};
}

Result<std::string> ReadUserIdFile() {
  return CF_EXPECT(ReadUniqueIdFile(CF_EXPECT(CvdDataHome()), kUserIdFileName));
}

Result<void> GenerateUserIdFile() {
  CF_EXPECT(GenerateUniqueIdFile(CF_EXPECT(CvdDataHome()), kUserIdFileName));
  return {};
}

}  // namespace cuttlefish
