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
  bool local_instance_set;
  std::optional<int> local_instance;
  std::optional<std::string> flavor;
  std::optional<std::string> local_kernel_image;
};

Result<ConverterParsed> ParseAcloudCreateFlags(cvd_common::Args& arguments);

}  // namespace acloud_impl
}  // namespace cuttlefish
