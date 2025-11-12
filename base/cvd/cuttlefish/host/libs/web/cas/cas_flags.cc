//
// Copyright (C) 2024 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/cas/cas_flags.h"

#include <stdint.h>

#include <string>
#include <vector>

#include <android-base/parseint.h>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {
namespace {

std::string GetDefaultCasConfigFilePath() {
  if (FileExists(kDefaultCasConfigFilePath)) {
    return kDefaultCasConfigFilePath;
  }
  return "";
}

std::string GetDefaultDownloaderPath() {
  if (FileExists(kDefaultDownloaderPath)) {
    return kDefaultDownloaderPath;
  }
  return "";
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

// Helper function to apply config file values while respecting command-line
// overrides. Call this after parsing command-line flags but before parsing
// config file values. This ensures the priority:
//   Command-line > Config File > Defaults
template <typename T>
void ApplyConfigValue(FlagValue<T>& flag_val, const T& config_value) {
  // Only override if user hasn't specified a value on the command-line
  if (!flag_val.user_specified) {
    flag_val.value = config_value;
    flag_val.user_specified = true;  // Mark as coming from config file
  }
}

}  // namespace

CasDownloaderFlags::CasDownloaderFlags() {
  // Set defaults (user_specified = false by default in FlagValue constructor)
  cas_config_filepath.value = GetDefaultCasConfigFilePath();
  downloader_path.value = GetDefaultDownloaderPath();
  prefer_uncompressed.value = false;
  cache_dir.value = "";
  cache_max_size.value = kMinCacheMaxSize;
  cache_lock.value = false;
  use_hardlink.value = true;
  memory_limit.value = kDefaultMemoryLimit;
  cas_concurrency.value = kDefaultCasConcurrency;
  rpc_timeout.value = kDefaultRpcTimeout;
  get_capabilities_timeout.value = kDefaultGetCapabilitiesTimeout;
  get_tree_timeout.value = kDefaultGetTreeTimeout;
  batch_read_blobs_timeout.value = kDefaultBatchReadBlobsTimeout;
  batch_update_blobs_timeout.value = kDefaultBatchUpdateBlobsTimeout;
  version.value = false;
}

std::vector<Flag> CasDownloaderFlags::Flags() {
  std::vector<Flag> flags;

  flags.emplace_back(
      GflagsCompatFlag("cas_config_filepath")
          .Getter([this]() { return this->cas_config_filepath.value; })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->cas_config_filepath.value = match.value;
            this->cas_config_filepath.user_specified = true;
            return {};
          })
          .Help("Path to the CAS downloader config file."));
  flags.emplace_back(
      GflagsCompatFlag("cas_downloader_path")
          .Getter([this]() { return this->downloader_path.value; })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->downloader_path.value = match.value;
            this->downloader_path.user_specified = true;
            return {};
          })
          .Help("Path to the CAS downloader binary. Enables CAS downloading if "
                "specified."));
  flags.emplace_back(
      GflagsCompatFlag("cas_prefer_uncompressed", this->prefer_uncompressed.value)
          .Getter([this]() { return fmt::format("{}", this->prefer_uncompressed.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->prefer_uncompressed.value = 
                CF_EXPECT(ParseBool(match.value, "cas_prefer_uncompressed"));
            this->prefer_uncompressed.user_specified = true;
            return {};
          })
          .Help("Download uncompressed artifacts if available."));
  flags.emplace_back(
      GflagsCompatFlag("cas_cache_dir")
          .Getter([this]() { return this->cache_dir.value; })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->cache_dir.value = match.value;
            this->cache_dir.user_specified = true;
            return {};
          })
          .Help("Cache directory to store downloaded files (casdownloader "
                "flag: cache-dir)."));
  flags.emplace_back(
      GflagsCompatFlagInt64("cas_cache_max_size", this->cache_max_size.value)
          .Getter([this]() { return std::to_string(this->cache_max_size.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            int64_t parsed_int;
            CF_EXPECTF(android::base::ParseInt(match.value, &parsed_int),
                       "Failed to parse \"{}\" as an integer (int64_t)",
                       match.value);
            this->cache_max_size.value = parsed_int;
            this->cache_max_size.user_specified = true;
            return {};
          })
          .Help("Cache is trimmed if the cache gets larger than "
                "this value in bytes (casdownloader flag: cache-max-size)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_cache_lock", this->cache_lock.value)
          .Getter([this]() { return fmt::format("{}", this->cache_lock.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->cache_lock.value = CF_EXPECT(ParseBool(match.value, "cas_cache_lock"));
            this->cache_lock.user_specified = true;
            return {};
          })
          .Help("Enable cache lock (casdownloader flag: cache-lock)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_use_hardlink", this->use_hardlink.value)
          .Getter([this]() { return fmt::format("{}", this->use_hardlink.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->use_hardlink.value = CF_EXPECT(ParseBool(match.value, "cas_use_hardlink"));
            this->use_hardlink.user_specified = true;
            return {};
          })
          .Help("By default local cache will use hardlink when push and pull "
                "files (casdownloader flag: use-hardlink)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_concurrency", this->cas_concurrency.value)
          .Getter([this]() { return std::to_string(this->cas_concurrency.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->cas_concurrency.value = CF_EXPECT(ParseInt(match.value, "cas_concurrency"));
            this->cas_concurrency.user_specified = true;
            return {};
          })
          .Help("the maximum number of concurrent download operations "
                "(casdownloader flag: cas-concurrency)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_memory_limit", this->memory_limit.value)
          .Getter([this]() { return std::to_string(this->memory_limit.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->memory_limit.value = CF_EXPECT(ParseInt(match.value, "cas_memory_limit"));
            this->memory_limit.user_specified = true;
            return {};
          })
          .Help("Memory limit in MiB (casdownloader flag: memory-limit)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_rpc_timeout", this->rpc_timeout.value)
          .Getter([this]() { return std::to_string(this->rpc_timeout.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->rpc_timeout.value = CF_EXPECT(ParseInt(match.value, "cas_rpc_timeout"));
            this->rpc_timeout.user_specified = true;
            return {};
          })
          .Help("Default RPC timeout in seconds (casdownloader flag: "
                "rpc-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_get_capabilities_timeout",
                       this->get_capabilities_timeout.value)
          .Getter([this]() { return std::to_string(this->get_capabilities_timeout.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->get_capabilities_timeout.value = 
                CF_EXPECT(ParseInt(match.value, "cas_get_capabilities_timeout"));
            this->get_capabilities_timeout.user_specified = true;
            return {};
          })
          .Help("RPC timeout for GetCapabilities in seconds (casdownloader "
                "flag: get-capabilities-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_get_tree_timeout", this->get_tree_timeout.value)
          .Getter([this]() { return std::to_string(this->get_tree_timeout.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->get_tree_timeout.value = CF_EXPECT(ParseInt(match.value, "cas_get_tree_timeout"));
            this->get_tree_timeout.user_specified = true;
            return {};
          })
          .Help("RPC timeout for GetTree in seconds "
                "(casdownloader flag: get-tree-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_batch_read_blobs_timeout",
                       this->batch_read_blobs_timeout.value)
          .Getter([this]() { return std::to_string(this->batch_read_blobs_timeout.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->batch_read_blobs_timeout.value = 
                CF_EXPECT(ParseInt(match.value, "cas_batch_read_blobs_timeout"));
            this->batch_read_blobs_timeout.user_specified = true;
            return {};
          })
          .Help("RPC timeout for BatchReadBlobs in seconds (casdownloader "
                "flag: batch-read-blobs-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_batch_update_blobs_timeout",
                       this->batch_update_blobs_timeout.value)
          .Getter([this]() { return std::to_string(this->batch_update_blobs_timeout.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->batch_update_blobs_timeout.value = 
                CF_EXPECT(ParseInt(match.value, "cas_batch_update_blobs_timeout"));
            this->batch_update_blobs_timeout.user_specified = true;
            return {};
          })
          .Help("RPC timeout for BatchUpdateBlobs in seconds (casdownloader "
                "flag: batch-update-blobs-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("version", this->version.value)
          .Getter([this]() { return fmt::format("{}", this->version.value); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->version.value = CF_EXPECT(ParseBool(match.value, "version"));
            this->version.user_specified = true;
            return {};
          })
          .Help("Print CAS downloader version information "
                "(casdownloader flag: version)."));

  return flags;
}

}  // namespace cuttlefish
