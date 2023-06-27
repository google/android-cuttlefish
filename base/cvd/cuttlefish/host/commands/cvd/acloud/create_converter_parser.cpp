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

#include "host/commands/cvd/acloud/create_converter_parser.h"

#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>

#include "common/libs/utils/flag_parser.h"
#include "host/commands/cvd/acloud/converter_parser_common.h"

namespace cuttlefish {
namespace acloud_impl {

Result<ConverterParsed> ParseAcloudCreateFlags(cvd_common::Args& arguments) {
  std::vector<Flag> flags;

  bool local_instance_set;
  std::optional<int> local_instance;
  auto local_instance_flag = Flag();
  local_instance_flag.Alias(
      {FlagAliasMode::kFlagConsumesArbitrary, "--local-instance"});
  local_instance_flag.Setter(
      [&local_instance_set, &local_instance](const FlagMatch& m) {
        local_instance_set = true;
        if (m.value != "" && local_instance) {
          LOG(ERROR) << "Instance number already set, was \"" << *local_instance
                     << "\", now set to \"" << m.value << "\"";
          return false;
        } else if (m.value != "" && !local_instance) {
          int value = -1;
          if (!android::base::ParseInt(m.value, &value)) {
            return false;
          }
          local_instance = value;
        }
        return true;
      });
  flags.emplace_back(local_instance_flag);

  std::optional<std::string> flavor;
  flags.emplace_back(CF_EXPECT(AcloudCompatFlag({"config", "flavor"}, flavor)));

  std::optional<std::string> local_kernel_image;
  flags.emplace_back(CF_EXPECT(AcloudCompatFlag(
      {"local-kernel-image", "local-boot-image"}, local_kernel_image)));

  std::optional<std::string> image_download_dir;
  flags.emplace_back(
      CF_EXPECT(AcloudCompatFlag({"image-download-dir"}, image_download_dir)));

  std::optional<std::string> local_system_image;
  flags.emplace_back(
      CF_EXPECT(AcloudCompatFlag({"local-system-image"}, local_system_image)));

  CF_EXPECT(ParseFlags(flags, arguments));
  return ConverterParsed{
      .local_instance_set = local_instance_set,
      .local_instance = local_instance,
      .flavor = flavor,
      .local_kernel_image = local_kernel_image,
      .image_download_dir = image_download_dir,
      .local_system_image = local_system_image,
  };
}

}  // namespace acloud_impl
}  // namespace cuttlefish
