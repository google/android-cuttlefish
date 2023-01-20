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

#include "host/commands/cvd/parser/fetch_cvd_parser.h"

#include <android-base/file.h>
#include <gflags/gflags.h>

#include <stdio.h>
#include <fstream>
#include <string>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "host/commands/assemble_cvd/flags_defaults.h"

namespace cuttlefish {

Result<void> ValidateFetchCvdConfigs(const Json::Value&) { return {}; }

std::vector<std::string> GenerateFetchCvdFlags(const Json::Value&) {
  std::vector<std::string> result;
  return result;
}

void InitFetchCvdConfigs(Json::Value&) {}

Result<std::vector<std::string>> ParseFetchCvdConfigs(Json::Value& root) {
  CF_EXPECT(ValidateFetchCvdConfigs(root), "Loaded Json validation failed");
  InitFetchCvdConfigs(root);
  return GenerateFetchCvdFlags(root);
}

}  // namespace cuttlefish
