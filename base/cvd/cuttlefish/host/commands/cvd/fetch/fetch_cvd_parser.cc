//
// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"

#include <cstddef>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kUsageMessage[] =
    "*_build flags other than --chrome_os_build accept values in the following "
    "format:\n"
    "{<branch> | <build_id>}[/<build_target>][{<filepath>}]\n"
    "For example: "
    "\"aosp-android-latest-release/"
    "aosp_cf_x86_64_only_phone-userdebug{file.txt}\""
    "<branch> fetches artifacts from the latest build of the argument\n"
    "{<filepath>} is used for certain artifacts to specify the file to "
    "download location in the build artifacts\n"
    "if <build_target> is not specified then the default build target is: ";

std::vector<Flag> GetFlagsVector(FetchFlags& fetch_flags,
                                 std::string& directory) {
  std::vector<Flag> flags;
  flags.emplace_back(
      GflagsCompatFlag("directory", directory)
          .Help("Target directory to fetch files into. (deprecated)"));
  flags.emplace_back(
      GflagsCompatFlag("target_directory", fetch_flags.target_directory)
          .Help("Target directory to fetch files into."));
  flags.emplace_back(GflagsCompatFlag("keep_downloaded_archives",
                                      fetch_flags.keep_downloaded_archives)
                         .Help("Keep downloaded zip/tar."));
  flags.emplace_back(
      GflagsCompatFlag("host_package_build", fetch_flags.host_package_build)
          .Help("source for the host cvd tools"));
  flags.emplace_back(
      GflagsCompatFlag("host_substitutions", fetch_flags.host_substitutions)
          .Help("list of executables to override with packaged versions."));

  for (Flag flag : fetch_flags.build_api_flags.Flags()) {
    flags.emplace_back(std::move(flag));
  }

  for (Flag flag : fetch_flags.vector_flags.Flags()) {
    flags.emplace_back(std::move(flag));
  }

  std::stringstream help_message;
  help_message << kUsageMessage << kDefaultBuildTarget;
  flags.emplace_back(HelpFlag(flags, help_message.str()));
  flags.emplace_back(
      HelpXmlFlag(flags, std::cout, fetch_flags.helpxml, help_message.str()));

  flags.emplace_back(UnexpectedArgumentGuard());
  return flags;
}

}  // namespace

Result<FetchFlags> FetchFlags::Parse(std::vector<std::string>& args) {
  FetchFlags fetch_flags;
  std::string directory;
  std::vector<Flag> flags = GetFlagsVector(fetch_flags, directory);
  CF_EXPECT(ConsumeFlags(flags, args), "Could not process command line flags.");

  if (!directory.empty()) {
    LOG(WARNING) << "Please use --target_directory instead of --directory";
    if (fetch_flags.target_directory.empty()) {
      fetch_flags.target_directory = directory;
    }
  } else {
    if (fetch_flags.target_directory.empty()) {
      fetch_flags.target_directory = CurrentDirectory();
    }
  }
  fetch_flags.target_directory = AbsolutePath(fetch_flags.target_directory);

  if (!fetch_flags.vector_flags.boot_artifact.empty()) {
    LOG(WARNING) << "Please use the build string filepath syntax instead of "
                  "deprecated --boot_artifact";
    for (const auto& build_string : fetch_flags.vector_flags.boot_build) {
      if (build_string) {
        CF_EXPECT(!GetFilepath(*build_string),
                  "Cannot use both the --boot_artifact flag and set the "
                  "filepath in the boot build string.  Please use only the "
                  "build string filepath");
      }
    }
  }

  if (!fetch_flags.build_api_flags.credential_source.empty()) {
    LOG(WARNING) << "Please use the new, specific credential flags instead of "
                  "the deprecated --credential_source";
  }
  CredentialFlags& credential_flags =
      fetch_flags.build_api_flags.credential_flags;
  const int number_of_set_credential_flags =
      !fetch_flags.build_api_flags.credential_source.empty() +
      credential_flags.use_gce_metadata +
      !credential_flags.credential_filepath.empty() +
      !credential_flags.service_account_filepath.empty();
  CF_EXPECT_LE(number_of_set_credential_flags, 1,
               "At most a single credential flag may be set.");

  CF_EXPECT(fetch_flags.vector_flags.NumberOfBuilds());

  return {fetch_flags};
}

}  // namespace cuttlefish
