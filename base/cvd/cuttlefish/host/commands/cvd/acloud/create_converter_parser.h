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

#include "common/libs/utils/result.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {
namespace acloud_impl {

struct ConverterParsed {
  struct LocalInstance {
    bool is_set;
    std::optional<int> id;
  } local_instance;
  std::optional<std::string> flavor;
  std::optional<std::string> local_kernel_image;
  std::optional<std::string> image_download_dir;
  std::optional<std::string> local_system_image;
  bool verbose;
  std::optional<std::string> branch;
  struct LocalImage {
    bool given;
    std::optional<std::string> path;
  } local_image;
  std::optional<std::string> build_id;
  std::optional<std::string> build_target;
  std::optional<std::string> config_file;
  struct Bootloader {
    std::optional<std::string> build_id;
    std::optional<std::string> build_target;
    std::optional<std::string> branch;
  } bootloader;
};

Result<ConverterParsed> ParseAcloudCreateFlags(cvd_common::Args& arguments);

// Parse and generates a `cvdr` command given an `acloud` command.
Result<cvd_common::Args> CompileFromAcloudToCvdr(cvd_common::Args& arguments);

}  // namespace acloud_impl
}  // namespace cuttlefish
