/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "host/commands/cvd/parser/load_configs_parser.h"

#include <android-base/file.h>
#include <gflags/gflags.h>

#include <stdio.h>
#include <fstream>
#include <string>

#include "common/libs/utils/files.h"
#include "common/libs/utils/json.h"
#include "host/commands/assemble_cvd/flags_defaults.h"
#include "host/commands/cvd/parser/cf_configs_common.h"
#include "host/commands/cvd/parser/cf_configs_instances.h"
#include "host/commands/cvd/parser/fetch_cvd_parser.h"
#include "host/commands/cvd/parser/launch_cvd_parser.h"

namespace cuttlefish {

Result<Json::Value> ParseJsonFile(const std::string& file_path) {
  std::string file_content;
  using android::base::ReadFileToString;
  CF_EXPECT(ReadFileToString(file_path.c_str(), &file_content,
                             /* follow_symlinks */ true));
  auto root = CF_EXPECT(ParseJson(file_content), "Failed parsing JSON file");
  return root;
}

Result<CvdFlags> ParseCvdConfigs(Json::Value& root) {
  CvdFlags results;
  results.launch_cvd_flags =
      CF_EXPECT(ParseLaunchCvdConfigs(root),
                "parsing json configs for launch_cvd failed");

  results.fetch_cvd_flags = CF_EXPECT(
      ParseFetchCvdConfigs(root), "parsing json configs for fetch_cvd failed");

  return results;
}

}  // namespace cuttlefish
