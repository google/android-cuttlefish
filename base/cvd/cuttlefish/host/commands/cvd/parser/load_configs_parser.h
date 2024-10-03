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

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common/libs/utils/result.h"

namespace cuttlefish {

struct LoadDirectories {
  std::string target_directory;
  std::vector<std::string> target_subdirectories;
  std::string launch_home_directory;
  std::string host_package_directory;
  std::string system_image_directory_flag_value;
};

struct CvdFlags {
  std::vector<std::string> launch_cvd_flags;
  std::vector<std::string> selector_flags;
  std::vector<std::string> fetch_cvd_flags;
  LoadDirectories load_directories;
  std::optional<std::string> group_name;
  std::vector<std::string> instance_names;
};

struct Override {
  std::string config_path;
  std::string new_value;
};

std::ostream& operator<<(std::ostream& out, const Override& override);

struct LoadFlags {
  std::vector<Override> overrides;
  std::string config_path;
  std::string credential_source;
  std::string project_id;
  std::string base_dir;
};

Result<LoadFlags> GetFlags(std::vector<std::string>& args,
                           const std::string& working_directory);

Result<CvdFlags> GetCvdFlags(const LoadFlags& flags);

};  // namespace cuttlefish
