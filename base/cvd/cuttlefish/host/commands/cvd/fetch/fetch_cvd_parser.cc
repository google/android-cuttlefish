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

#include "host/commands/cvd/fetch/fetch_cvd_parser.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include "gflags/gflags.h"

#include "common/libs/utils/files.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/result.h"
#include "host/libs/web/android_build_string.h"
#include "host/libs/web/cas/cas_downloader.h"

namespace cuttlefish {
namespace {

constexpr char kUsageMessage[] =
    "*_build flags other than --chrome_os_build accept values in the following "
    "format:\n"
    "{<branch> | <build_id>}[/<build_target>][{<filepath>}]\n"
    "For example: "
    "\"aosp-main/aosp_cf_x86_64_phone-trunk_staging-userdebug{file.txt}\""
    "<branch> fetches artifacts from the latest build of the argument\n"
    "{<filepath>} is used for certain artifacts to specify the file to "
    "download location in the build artifacts\n"
    "if <build_target> is not specified then the default build target is: ";

Flag GflagsCompatFlagSeconds(const std::string& name,
                             std::chrono::seconds& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return std::to_string(value.count()); })
      .Setter([&value](const FlagMatch& match) -> Result<void> {
        int parsed_int;
        CF_EXPECTF(android::base::ParseInt(match.value, &parsed_int),
                   "Failed to parse \"{}\" as an integer", match.value);
        value = std::chrono::seconds(parsed_int);
        return {};
      });
}

Flag GflagsCompatFlagInt64(const std::string& name, int64_t& value) {
  return GflagsCompatFlag(name)
      .Getter([&value]() { return std::to_string(value); })
      .Setter([&value](const FlagMatch& match) -> Result<void> {
        int64_t parsed_int;
        CF_EXPECTF(android::base::ParseInt(match.value, &parsed_int),
                   "Failed to parse \"{}\" as an integer (int64_t)",
                   match.value);
        value = parsed_int;
        return {};
      });
}

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
  flags.emplace_back(VerbosityFlag(fetch_flags.verbosity));
  flags.emplace_back(
      GflagsCompatFlag("target_subdirectory", fetch_flags.target_subdirectory)
          .Help("Target subdirectory to fetch files into.  Specifically aimed "
                "at organizing builds when there are multiple fetches. "
                "**Note**: directory separator automatically prepended, only "
                "give the subdirectory name."));
  flags.emplace_back(
      GflagsCompatFlag("host_package_build", fetch_flags.host_package_build)
          .Help("source for the host cvd tools"));

  BuildApiFlags& build_api_flags = fetch_flags.build_api_flags;
  flags.emplace_back(GflagsCompatFlag("api_key", build_api_flags.api_key)
                         .Help("API key ofr the Android Build API"));
  flags.emplace_back(
      GflagsCompatFlag("credential_source", build_api_flags.credential_source)
          .Help("Build API credential source"));
  flags.emplace_back(
      GflagsCompatFlag("project_id", build_api_flags.project_id)
          .Help("Project ID used to access the Build API"));
  flags.emplace_back(GflagsCompatFlagSeconds("wait_retry_period",
                                             build_api_flags.wait_retry_period)
                         .Help("Retry period for pending builds given in "
                               "seconds. Set to 0 to not wait."));
  flags.emplace_back(
      GflagsCompatFlag("external_dns_resolver",
                       build_api_flags.external_dns_resolver)
          .Help("Use an out-of-process mechanism to resolve DNS queries"));
  flags.emplace_back(
      GflagsCompatFlag("api_base_url", build_api_flags.api_base_url)
          .Help("The base url for API requests to download artifacts from"));
  flags.emplace_back(
      GflagsCompatFlag("enable_caching", build_api_flags.enable_caching)
          .Help("Whether to enable local fetch file caching or not"));

  CredentialFlags& credential_flags = build_api_flags.credential_flags;
  flags.emplace_back(
      GflagsCompatFlag("use_gce_metadata", credential_flags.use_gce_metadata)
          .Help("Enforce using GCE metadata credentials."));
  flags.emplace_back(
      GflagsCompatFlag("credential_filepath",
                       credential_flags.credential_filepath)
          .Help("Enforce reading credentials from the given filepath."));
  flags.emplace_back(GflagsCompatFlag("service_account_filepath",
                                      credential_flags.service_account_filepath)
                         .Help("Enforce reading service account credentials "
                               "from the given filepath."));

  // Most of these flags are passed to casdownloader (a go binary). A prefix
  // "cas-" is added to the flag name if it doesn't already have one to minimize
  // chances of flag name conflicts and "-" is replaced with "_" for consistency
  // with cvd flags. E.g. the casdownloader flag "cache-dir" becomes cvd flag
  // "cas_cache_dir".
  CasDownloaderFlags& cas_downloader_flags =
      fetch_flags.build_api_flags.cas_downloader_flags;
  flags.emplace_back(GflagsCompatFlag("cas_config_filepath",
                                      cas_downloader_flags.cas_config_filepath)
                         .Help("Path to the CAS downloader config file. Other "
                               "CAS flags will be ignored if this is set."));
  flags.emplace_back(
      GflagsCompatFlag("cas_downloader_path",
                       cas_downloader_flags.downloader_path)
          .Help("Path to the CAS downloader binary. Enables CAS downloading if "
                "specified."));
  flags.emplace_back(
      GflagsCompatFlag("cas_prefer_uncompressed",
                       cas_downloader_flags.prefer_uncompressed)
          .Help("Download uncompressed artifacts if available."));
  flags.emplace_back(
      GflagsCompatFlag("cas_cache_dir", cas_downloader_flags.cache_dir)
          .Help("Cache directory to store downloaded files (casdownloader "
                "flag: cache-dir)."));
  flags.emplace_back(
      GflagsCompatFlagInt64("cas_cache_max_size",
                            cas_downloader_flags.cache_max_size)
          .Help("Cache is trimmed if the cache gets larger than "
                "this value in bytes (casdownloader flag: cache-max-size)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_cache_lock", cas_downloader_flags.use_hardlink)
          .Help("Enable cache lock (casdownloader flag: cache-lock)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_use_hardlink", cas_downloader_flags.use_hardlink)
          .Help("By default local cache will use hardlink when push and pull "
                "files (casdownloader flag: use-hardlink)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_concurrency", cas_downloader_flags.cas_concurrency)
          .Help("the maximum number of concurrent download operations "
                "(casdownloader flag: cas-concurrency)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_memory_limit", cas_downloader_flags.memory_limit)
          .Help("Memory limit in MiB (casdownloader flag: memory-limit)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_rpc_timeout", cas_downloader_flags.rpc_timeout)
          .Help("Default RPC timeout in seconds (casdownloader flag: "
                "rpc-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_get_capabilities_timeout",
                       cas_downloader_flags.get_capabilities_timeout)
          .Help("RPC timeout for GetCapabilities in seconds (casdownloader "
                "flag: get-capabilities-timeout)."));
  flags.emplace_back(GflagsCompatFlag("cas_get_tree_timeout",
                                      cas_downloader_flags.get_tree_timeout)
                         .Help("RPC timeout for GetTree in seconds "
                               "(casdownloader flag: get-tree-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_batch_read_blobs_timeout",
                       cas_downloader_flags.batch_read_blobs_timeout)
          .Help("RPC timeout for BatchReadBlobs in seconds (casdownloader "
                "flag: batch-read-blobs-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_batch_update_blobs_timeout",
                       cas_downloader_flags.batch_update_blobs_timeout)
          .Help("RPC timeout for BatchUpdateBlobs in seconds (casdownloader "
                "flag: batch-update-blobs-timeout)."));
  flags.emplace_back(GflagsCompatFlag("version", cas_downloader_flags.version)
                         .Help("Print CAS downloader version information "
                               "(casdownloader flag: version)."));

  VectorFlags& vector_flags = fetch_flags.vector_flags;
  flags.emplace_back(
      GflagsCompatFlag("default_build", vector_flags.default_build)
          .Help("source for the cuttlefish build to use (vendor.img + host)"));
  flags.emplace_back(GflagsCompatFlag("system_build", vector_flags.system_build)
                         .Help("source for system.img and product.img"));
  flags.emplace_back(GflagsCompatFlag("kernel_build", vector_flags.kernel_build)
                         .Help("source for the kernel or gki target"));
  flags.emplace_back(GflagsCompatFlag("boot_build", vector_flags.boot_build)
                         .Help("source for the boot or gki target"));
  flags.emplace_back(
      GflagsCompatFlag("bootloader_build", vector_flags.bootloader_build)
          .Help("source for the bootloader target"));
  flags.emplace_back(GflagsCompatFlag("android_efi_loader_build",
                                      vector_flags.android_efi_loader_build)
                         .Help("source for the uefi app target"));
  flags.emplace_back(
      GflagsCompatFlag("otatools_build", vector_flags.otatools_build)
          .Help("source for the host ota tools"));
  flags.emplace_back(
      GflagsCompatFlag("chrome_os_build", vector_flags.chrome_os_build)
          .Help("source for a ChromeOS build. Formatted as as a numeric build "
                "id, or '<project>/<bucket>/<builder>'"));

  flags.emplace_back(
      GflagsCompatFlag("boot_artifact", vector_flags.boot_artifact)
          .Help("name of the boot image in boot_build"));
  flags.emplace_back(GflagsCompatFlag("download_img_zip",
                                      vector_flags.download_img_zip,
                                      kDefaultDownloadImgZip)
                         .Help("Whether to fetch the -img-*.zip file."));
  flags.emplace_back(
      GflagsCompatFlag("download_target_files_zip",
                       vector_flags.download_target_files_zip,
                       kDefaultDownloadTargetFilesZip)
          .Help("Whether to fetch the -target_files-*.zip file."));

  std::stringstream help_message;
  help_message << kUsageMessage << kDefaultBuildTarget;
  flags.emplace_back(HelpFlag(flags, help_message.str()));
  flags.emplace_back(
      HelpXmlFlag(flags, std::cout, fetch_flags.helpxml, help_message.str()));

  flags.emplace_back(UnexpectedArgumentGuard());
  return flags;
}

Result<int> GetNumberOfBuilds(
    const VectorFlags& flags,
    const std::vector<std::string>& subdirectory_flag) {
  std::optional<std::size_t> number_of_builds;
  for (const auto& flag_size :
       {flags.default_build.size(), flags.system_build.size(),
        flags.kernel_build.size(), flags.boot_build.size(),
        flags.bootloader_build.size(), flags.android_efi_loader_build.size(),
        flags.otatools_build.size(), flags.chrome_os_build.size(),
        flags.boot_artifact.size(), flags.download_img_zip.size(),
        flags.download_target_files_zip.size(), subdirectory_flag.size()}) {
    if (flag_size == 0) {
      // a size zero flag vector means the flag was not given
      continue;
    }
    if (number_of_builds) {
      CF_EXPECT(
          flag_size == *number_of_builds,
          "Mismatched flag lengths: " << *number_of_builds << "," << flag_size);
    }
    number_of_builds = flag_size;
  }
  // if no flags had values there is 1 all-default build
  return number_of_builds.value_or(1);
}

}  // namespace

Result<FetchFlags> GetFlagValues(std::vector<std::string>& args) {
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

  fetch_flags.number_of_builds = CF_EXPECT(GetNumberOfBuilds(
      fetch_flags.vector_flags, fetch_flags.target_subdirectory));
  return {fetch_flags};
}

}  // namespace cuttlefish
