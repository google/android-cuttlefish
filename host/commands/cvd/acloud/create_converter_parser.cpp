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

#include <array>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "common/libs/utils/contains.h"
#include "common/libs/utils/flag_parser.h"

namespace cuttlefish {
namespace {

constexpr char kFlagConfig[] = "config";
constexpr char kFlagBranch[] = "branch";
constexpr char kFlagBuildId[] = "build_id";
constexpr char kFlagBuildTarget[] = "build_target";
constexpr char kFlagConfigFile[] = "config_file";
constexpr char kFlagLocalKernelImage[] = "local-kernel-image";
constexpr char kFlagLocalSystemImage[] = "local-system-image";
constexpr char kFlagBootloaderBuildId[] = "bootloader_build_id";
constexpr char kFlagBootloaderBuildTarget[] = "bootloader_build_target";
constexpr char kFlagBootloaderBranch[] = "bootloader_branch";
constexpr char kFlagImageDownloadDir[] = "image-download-dir";
constexpr char kFlagLocalImage[] = "local-image";
constexpr char kFlagLocalInstance[] = "local-instance";

constexpr char kAcloudCmdCreate[] = "create";
constexpr char kAcloudCmdList[] = "list";
constexpr char kAcloudCmdDelete[] = "delete";
constexpr char kAcloudCmdReconnect[] = "reconnect";
constexpr char kAcloudCmdPowerdash[] = "reconnect";
constexpr char kAcloudCmdPull[] = "pull";
constexpr char kAcloudCmdRestart[] = "restart";
constexpr char kAcloudCmdHostCleanup[] = "hostcleanup";

constexpr std::array kAcloudCommands = {
    kAcloudCmdCreate,    kAcloudCmdList,        kAcloudCmdDelete,
    kAcloudCmdReconnect, kAcloudCmdPowerdash,   kAcloudCmdPull,
    kAcloudCmdRestart,   kAcloudCmdHostCleanup,
};

struct VerboseParser {
  std::optional<bool> token;

  Flag Parser() {
    return Flag()
        .Alias({FlagAliasMode::kFlagExact, "-v"})
        .Alias({FlagAliasMode::kFlagExact, "-vv"})
        .Alias({FlagAliasMode::kFlagExact, "--verbose"})
        .Setter([this](const FlagMatch&) -> Result<void> {
          token = true;
          return {};
        });
  }
};

struct StringParser {
  StringParser(const char* orig) : StringParser(orig, "", false) {}
  StringParser(const char* orig, bool allow_empty)
      : StringParser(orig, "", allow_empty) {}
  StringParser(const char* orig, const char* alias)
      : StringParser(orig, alias, false) {}
  StringParser(const char* orig, const char* alias, bool allow_empty)
      : orig(orig), alias(alias), allow_empty(allow_empty), token({}) {}
  std::string orig;
  std::string alias;
  bool allow_empty;
  std::optional<std::string> token;

  Flag Parser() {
    Flag parser;
    FlagAliasMode mode = allow_empty ? FlagAliasMode::kFlagConsumesArbitrary
                                     : FlagAliasMode::kFlagConsumesFollowing;
    parser.Alias({mode, "--" + orig});
    if (!alias.empty()) {
      parser.Alias({mode, "--" + alias});
    }
    parser.Setter([this](const FlagMatch& m) -> Result<void> {
      // Multiple matches could happen when kFlagConsumesArbitrary is used, the
      // empty string match would be always the last one.
      if (!token.has_value()) {
        token = m.value;
      } else if (!m.value.empty()) {
        return CF_ERRF("\"{}\" already set, was \"{}\", now set to \"{}\"",
                       orig, token.value(), m.value);
      }
      return {};
    });
    return parser;
  }
};

template <typename K, typename V>
std::optional<V> GetOptVal(const std::unordered_map<K, V>& m, const K& key) {
  auto it = m.find(key);
  return it == m.end() ? std::optional<V>() : it->second;
}

struct Tokens {
  std::unordered_map<std::string, std::string> strings;
  std::unordered_map<std::string, bool> booleans;

  std::optional<std::string> StringVal(std::string name) {
    return GetOptVal(strings, name);
  }

  std::optional<bool> BoolVal(std::string name) {
    return GetOptVal(booleans, name);
  }
};

Result<Tokens> ParseForCvdCreate(cvd_common::Args& arguments) {
  std::vector<StringParser> string_parsers = {
      StringParser(kFlagBranch),
      StringParser(kFlagLocalSystemImage),
      StringParser(kFlagImageDownloadDir),
      StringParser(kFlagConfig, "flavor"),
      StringParser(kFlagBuildId, "build-id"),
      StringParser(kFlagBuildTarget, "build-target"),
      StringParser(kFlagConfigFile, "config-file"),
      StringParser(kFlagLocalKernelImage, "local-boot-image"),
      StringParser(kFlagBootloaderBuildId, "bootloader-build-id"),
      StringParser(kFlagBootloaderBuildTarget, "bootloader-build-target"),
      StringParser(kFlagBootloaderBranch, "bootloader-branch"),
      StringParser(kFlagLocalImage, true),
      StringParser(kFlagLocalInstance, true),
  };
  VerboseParser verbose_parser = VerboseParser{};

  std::vector<Flag> parsers;

  for (auto& p : string_parsers) {
    parsers.emplace_back(p.Parser());
  }
  parsers.emplace_back(verbose_parser.Parser());

  CF_EXPECT(ParseFlags(parsers, arguments));

  auto result = Tokens{};
  for (auto& p : string_parsers) {
    if (p.token.has_value()) {
      result.strings[p.orig] = p.token.value();
    }
  }
  if (verbose_parser.token.has_value()) {
    result.booleans["v"] = true;
  }
  return result;
}

Result<Tokens> ParseForCvdrCreate(cvd_common::Args& arguments) {
  std::vector<StringParser> string_parsers = {
      StringParser(kFlagBranch),
      StringParser(kFlagBuildId, "build-id"),
      StringParser(kFlagBuildTarget, "build-target"),
      StringParser(kFlagBootloaderBuildId, "bootloader-build-id"),
      StringParser(kFlagBootloaderBuildTarget, "bootloader-build-target"),
      StringParser(kFlagBootloaderBranch, "bootloader-branch"),
      StringParser(kFlagLocalImage, true),
  };

  std::vector<Flag> parsers;

  for (auto& p : string_parsers) {
    parsers.emplace_back(p.Parser());
  }

  CF_EXPECT(ParseFlags(parsers, arguments));

  auto result = Tokens{};
  for (auto& p : string_parsers) {
    if (p.token.has_value()) {
      result.strings[p.orig] = p.token.value();
    }
  }
  return result;
}

}  // namespace
namespace acloud_impl {

Result<ConverterParsed> ParseAcloudCreateFlags(cvd_common::Args& arguments) {
  auto tokens = CF_EXPECT(ParseForCvdCreate(arguments));
  std::optional<std::string> local_instance =
      tokens.StringVal(kFlagLocalInstance);
  std::optional<int> local_instance_id;
  if (local_instance.has_value() && !local_instance.value().empty()) {
    int value = -1;
    CF_EXPECTF(android::base::ParseInt(local_instance.value(), &value),
               "Invalid integer value for flag \"{}\": \"{}\"",
               kFlagLocalInstance, local_instance.value());
    local_instance_id = value;
  }
  std::optional<std::string> local_image = tokens.StringVal(kFlagLocalImage);
  std::optional<std::string> local_image_path;
  if (local_image.has_value() && !local_image.value().empty()) {
    local_image_path = local_image.value();
  }
  return ConverterParsed{
      .local_instance = {.is_set = local_instance.has_value(),
                         .id = local_instance_id},
      .flavor = tokens.StringVal(kFlagConfig),
      .local_kernel_image = tokens.StringVal(kFlagLocalKernelImage),
      .image_download_dir = tokens.StringVal(kFlagImageDownloadDir),
      .local_system_image = tokens.StringVal(kFlagLocalSystemImage),
      .verbose = tokens.BoolVal("v").has_value(),
      .branch = tokens.StringVal(kFlagBranch),
      .local_image =
          {
              .given = local_image.has_value(),
              .path = local_image_path,
          },
      .build_id = tokens.StringVal(kFlagBuildId),
      .build_target = tokens.StringVal(kFlagBuildTarget),
      .config_file = tokens.StringVal(kFlagConfigFile),
      .bootloader =
          {
              .build_id = tokens.StringVal(kFlagBootloaderBuildId),
              .build_target = tokens.StringVal(kFlagBuildTarget),
              .branch = tokens.StringVal(kFlagBootloaderBranch),
          },
  };
}

Result<cvd_common::Args> CompileFromAcloudToCvdr(cvd_common::Args& arguments) {
  std::vector<std::string> result;
  CF_EXPECT(arguments.size() > 0);
  CF_EXPECT(Contains(kAcloudCommands, arguments[0]));
  result.emplace_back(arguments[0]);
  arguments.erase(arguments.begin());
  // Only `acloud create` works with extra arguments/flags.
  CF_EXPECT(result[0] == kAcloudCmdCreate || arguments.empty());
  if (result[0] == kAcloudCmdCreate) {
    auto tokens = CF_EXPECT(ParseForCvdrCreate(arguments));
    CF_EXPECTF(arguments.empty(), "Unrecognized arguments: '{}'",
               fmt::join(arguments, "', '"));
    for (const auto& t : tokens.strings) {
      result.emplace_back("--" + t.first);
      result.emplace_back(t.second);
    }
  }
  return result;
}

}  // namespace acloud_impl
}  // namespace cuttlefish
