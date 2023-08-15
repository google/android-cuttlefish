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

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <json/json.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {

struct FetchCvdInstanceConfig {
  bool should_fetch = false;
  std::optional<std::string> default_build;
  std::optional<std::string> system_build;
  std::optional<std::string> kernel_build;
  std::optional<std::string> boot_build;
  std::optional<std::string> bootloader_build;
  std::optional<std::string> otatools_build;
  std::optional<std::string> host_package_build;
  std::optional<std::string> download_img_zip;
  std::optional<std::string> download_target_files_zip;
};

struct FetchCvdConfig {
  std::optional<std::string> api_key;
  std::optional<std::string> credential_source;
  std::optional<std::string> wait_retry_period;
  std::optional<std::string> external_dns_resolver;
  std::optional<std::string> keep_downloaded_archives;
  std::vector<FetchCvdInstanceConfig> instances;
};

Result<FetchCvdConfig> ParseFetchCvdConfigs(Json::Value& root);

};  // namespace cuttlefish
