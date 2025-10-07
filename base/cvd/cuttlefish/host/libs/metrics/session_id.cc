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

#include "cuttlefish/host/libs/metrics/session_id.h"

#include <string>

#include <uuid/uuid.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {
namespace {

constexpr char kSessionIdFileName[] = "metrics_session_id.txt";
constexpr int kUuidStringLength = 36;  // per uuid_unparse(3)

std::string GenerateUuid() {
  uuid_t uuid;
  uuid_generate_random(uuid);
  std::string uuid_str = std::string(kUuidStringLength, 'x');
  uuid_unparse(uuid, uuid_str.data());
  return uuid_str;
}

}  // namespace

Result<std::string> ReadSessionIdFile(const std::string& metrics_directory) {
  return CF_EXPECT(ReadFileContents(metrics_directory + kSessionIdFileName));
}

Result<void> GenerateSessionIdFile(const std::string& metrics_directory) {
  const std::string session_id = GenerateUuid();
  CF_EXPECT(WriteNewFile(metrics_directory + kSessionIdFileName, session_id));
  return {};
}

}  // namespace cuttlefish
