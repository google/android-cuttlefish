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

static Flag LocalInstanceFlag(bool& local_instance_set,
                              std::optional<int>& local_instance) {
  auto local_instance_flag = Flag();
  local_instance_flag.Alias(
      {FlagAliasMode::kFlagConsumesArbitrary, "--local-instance"});
  local_instance_flag.Setter([&local_instance_set, &local_instance](
                                 const FlagMatch& m) -> Result<void> {
    local_instance_set = true;
    if (m.value != "" && local_instance) {
      return CF_ERRF(
          "Instance number already set, was \"{}\", now set to \"{}\"",
          *local_instance, m.value);
    } else if (m.value != "" && !local_instance) {
      int value = -1;
      CF_EXPECTF(android::base::ParseInt(m.value, &value),
                 "Failed to parse \"{}\"", m.value);
      local_instance = value;
    }
    return {};
  });
  return local_instance_flag;
}

static Flag VerboseFlag(bool& verbose) {
  auto verbose_flag = Flag()
                          .Alias({FlagAliasMode::kFlagExact, "-v"})
                          .Alias({FlagAliasMode::kFlagExact, "-vv"})
                          .Alias({FlagAliasMode::kFlagExact, "--verbose"})
                          .Setter([&verbose](const FlagMatch&) -> Result<void> {
                            verbose = true;
                            return {};
                          });
  return verbose_flag;
}

static Flag LocalImageFlag(bool& local_image_given,
                           std::optional<std::string>& local_image_path) {
  return Flag()
      .Alias({FlagAliasMode::kFlagConsumesArbitrary, "--local-image"})
      .Setter([&local_image_given,
               &local_image_path](const FlagMatch& m) -> Result<void> {
        local_image_given = true;
        if (m.value != "") {
          local_image_path = m.value;
        }
        return {};
      });
}

Result<ConverterParsed> ParseAcloudCreateFlags(cvd_common::Args& arguments) {
  std::vector<Flag> flags;

  bool local_instance_set = false;
  std::optional<int> local_instance;
  flags.emplace_back(LocalInstanceFlag(local_instance_set, local_instance));

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

  bool verbose = false;
  flags.emplace_back(VerboseFlag(verbose));

  std::optional<std::string> branch;
  flags.emplace_back(CF_EXPECT(AcloudCompatFlag({"branch"}, branch)));

  bool local_image_given = false;
  std::optional<std::string> local_image_path;
  flags.emplace_back(LocalImageFlag(local_image_given, local_image_path));

  std::optional<std::string> build_id;
  flags.emplace_back(
      CF_EXPECT(AcloudCompatFlag({"build-id", "build_id"}, build_id)));

  std::optional<std::string> build_target;
  flags.emplace_back(CF_EXPECT(
      AcloudCompatFlag({"build-target", "build_target"}, build_target)));

  std::optional<std::string> config_file;
  flags.emplace_back(
      CF_EXPECT(AcloudCompatFlag({"config-file", "config_file"}, config_file)));

  std::optional<std::string> bootloader_build_id;
  flags.emplace_back(CF_EXPECT(AcloudCompatFlag(
      {"bootloader-build-id", "bootloader_build_id"}, bootloader_build_id)));
  std::optional<std::string> bootloader_build_target;
  flags.emplace_back(CF_EXPECT(
      AcloudCompatFlag({"bootloader-build-target", "bootloader_build_target"},
                       bootloader_build_target)));
  std::optional<std::string> bootloader_branch;
  flags.emplace_back(CF_EXPECT(AcloudCompatFlag(
      {"bootloader-branch", "bootloader_branch"}, bootloader_build_target)));

  CF_EXPECT(ParseFlags(flags, arguments));
  return ConverterParsed{
      .local_instance = {.is_set = local_instance_set, .id = local_instance},
      .flavor = flavor,
      .local_kernel_image = local_kernel_image,
      .image_download_dir = image_download_dir,
      .local_system_image = local_system_image,
      .verbose = verbose,
      .branch = branch,
      .local_image = {.given = local_image_given, .path = local_image_path},
      .build_id = build_id,
      .build_target = build_target,
      .config_file = config_file,
      .bootloader =
          {
              .build_id = bootloader_build_id,
              .build_target = bootloader_build_target,
              .branch = bootloader_branch,
          },
  };
}

}  // namespace acloud_impl
}  // namespace cuttlefish
