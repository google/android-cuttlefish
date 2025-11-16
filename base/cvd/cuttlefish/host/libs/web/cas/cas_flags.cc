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
#include <fmt/format.h>

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

Flag GflagsCompatFlagInt64(const std::string& name, FlagValue<int64_t>& flag) {
  return GflagsCompatFlag(name)
      .Getter([&flag]() { return std::to_string(flag.value()); })
      .Setter([&flag](const FlagMatch& match) -> Result<void> {
        int64_t parsed_int;
        CF_EXPECTF(android::base::ParseInt(match.value, &parsed_int),
                   "Failed to parse \"{}\" as an integer (int64_t)",
                   match.value);
        flag.set_value(parsed_int);
        return {};
      });
}

}  // namespace

CasDownloaderFlags::CasDownloaderFlags()
    : cas_config_filepath(GetDefaultCasConfigFilePath()),
      downloader_path(GetDefaultDownloaderPath()),
      prefer_uncompressed(false),
      cache_dir(""),
      invocation_id(""),
      cache_max_size(kMinCacheMaxSize),
      cache_lock(false),
      use_hardlink(true),
      memory_limit(kDefaultMemoryLimit),
      cas_concurrency(kDefaultCasConcurrency),
      rpc_timeout(kDefaultRpcTimeout),
      get_capabilities_timeout(kDefaultGetCapabilitiesTimeout),
      get_tree_timeout(kDefaultGetTreeTimeout),
      batch_read_blobs_timeout(kDefaultBatchReadBlobsTimeout),
      batch_update_blobs_timeout(kDefaultBatchUpdateBlobsTimeout),
      version(false) {}

std::vector<Flag> CasDownloaderFlags::Flags() {
  std::vector<Flag> flags;

  flags.emplace_back(
      GflagsCompatFlag("cas_config_filepath")
          .Getter([this]() { return this->cas_config_filepath.value(); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->cas_config_filepath.set_value(match.value);
            return {};
          })
          .Help("Path to the CAS downloader config file."));
  flags.emplace_back(
      GflagsCompatFlag("cas_downloader_path")
          .Getter([this]() { return this->downloader_path.value(); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->downloader_path.set_value(match.value);
            return {};
          })
          .Help("Path to the CAS downloader binary. Enables CAS downloading if "
                "specified."));
  flags.emplace_back(
      GflagsCompatFlag("cas_prefer_uncompressed")
          .Getter([this]() {
            return fmt::format("{}", this->prefer_uncompressed.value());
          })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->prefer_uncompressed.set_value(
                CF_EXPECT(ParseBool(match.value, "cas_prefer_uncompressed")));
            return {};
          })
          .Help("Download uncompressed artifacts if available."));
  flags.emplace_back(
      GflagsCompatFlag("cas_cache_dir")
          .Getter([this]() { return this->cache_dir.value(); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->cache_dir.set_value(match.value);
            return {};
          })
          .Help("Cache directory to store downloaded files (casdownloader "
                "flag: cache-dir)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_invocation_id")
          .Getter([this]() { return this->invocation_id.value(); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->invocation_id.set_value(match.value);
            return {};
          })
          .Help("Optional invocation identifier to tag CAS downloader runs "
                "(casdownloader flag: invocation-id)."));
  flags.emplace_back(
      GflagsCompatFlagInt64("cas_cache_max_size", this->cache_max_size)
          .Help("Cache is trimmed if the cache gets larger than "
                "this value in bytes (casdownloader flag: cache-max-size)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_cache_lock")
          .Getter(
              [this]() { return fmt::format("{}", this->cache_lock.value()); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->cache_lock.set_value(
                CF_EXPECT(ParseBool(match.value, "cas_cache_lock")));
            return {};
          })
          .Help("Enable cache lock (casdownloader flag: cache-lock)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_use_hardlink")
          .Getter([this]() {
            return fmt::format("{}", this->use_hardlink.value());
          })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->use_hardlink.set_value(
                CF_EXPECT(ParseBool(match.value, "cas_use_hardlink")));
            return {};
          })
          .Help("By default local cache will use hardlink when push and pull "
                "files (casdownloader flag: use-hardlink)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_concurrency")
          .Getter([this]() {
            return std::to_string(this->cas_concurrency.value());
          })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->cas_concurrency.set_value(
                CF_EXPECT(ParseInt(match.value, "cas_concurrency")));
            return {};
          })
          .Help("the maximum number of concurrent download operations "
                "(casdownloader flag: cas-concurrency)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_memory_limit")
          .Getter(
              [this]() { return std::to_string(this->memory_limit.value()); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->memory_limit.set_value(
                CF_EXPECT(ParseInt(match.value, "cas_memory_limit")));
            return {};
          })
          .Help("Memory limit in MiB (casdownloader flag: memory-limit)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_rpc_timeout")
          .Getter(
              [this]() { return std::to_string(this->rpc_timeout.value()); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->rpc_timeout.set_value(
                CF_EXPECT(ParseInt(match.value, "cas_rpc_timeout")));
            return {};
          })
          .Help("Default RPC timeout in seconds (casdownloader flag: "
                "rpc-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_get_capabilities_timeout")
          .Getter([this]() {
            return std::to_string(this->get_capabilities_timeout.value());
          })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->get_capabilities_timeout.set_value(CF_EXPECT(
                ParseInt(match.value, "cas_get_capabilities_timeout")));
            return {};
          })
          .Help("RPC timeout for GetCapabilities in seconds (casdownloader "
                "flag: get-capabilities-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_get_tree_timeout")
          .Getter([this]() {
            return std::to_string(this->get_tree_timeout.value());
          })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->get_tree_timeout.set_value(
                CF_EXPECT(ParseInt(match.value, "cas_get_tree_timeout")));
            return {};
          })
          .Help("RPC timeout for GetTree in seconds "
                "(casdownloader flag: get-tree-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_batch_read_blobs_timeout")
          .Getter([this]() {
            return std::to_string(this->batch_read_blobs_timeout.value());
          })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->batch_read_blobs_timeout.set_value(CF_EXPECT(
                ParseInt(match.value, "cas_batch_read_blobs_timeout")));
            return {};
          })
          .Help("RPC timeout for BatchReadBlobs in seconds (casdownloader "
                "flag: batch-read-blobs-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_batch_update_blobs_timeout")
          .Getter([this]() {
            return std::to_string(this->batch_update_blobs_timeout.value());
          })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->batch_update_blobs_timeout.set_value(CF_EXPECT(
                ParseInt(match.value, "cas_batch_update_blobs_timeout")));
            return {};
          })
          .Help("RPC timeout for BatchUpdateBlobs in seconds (casdownloader "
                "flag: batch-update-blobs-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("version")
          .Getter([this]() { return fmt::format("{}", this->version.value()); })
          .Setter([this](const FlagMatch& match) -> Result<void> {
            this->version.set_value(
                CF_EXPECT(ParseBool(match.value, "version")));
            return {};
          })
          .Help("Print CAS downloader version information "
                "(casdownloader flag: version)."));

  return flags;
}

}  // namespace cuttlefish
