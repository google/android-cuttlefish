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

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/flag_parser/flag.h"

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
      cas_config_filepath.Flag("cas_config_filepath")
          .Help("Path to the CAS downloader config file."));
  flags.emplace_back(
      downloader_path.Flag("cas_downloader_path")
          .Help("Path to the CAS downloader binary. Enables CAS downloading if "
                "specified."));
  flags.emplace_back(
      prefer_uncompressed.Flag("cas_prefer_uncompressed")
          .Help("Download uncompressed artifacts if available."));
  flags.emplace_back(
      cache_dir.Flag("cas_cache_dir")
          .Help("Cache directory to store downloaded files (casdownloader "
                "flag: cache-dir)."));
  flags.emplace_back(
      invocation_id.Flag("cas_invocation_id")
          .Help("Optional invocation identifier to tag CAS downloader runs "
                "(casdownloader flag: invocation-id)."));
  flags.emplace_back(
      cache_max_size.Flag("cas_cache_max_size")
          .Help("Cache is trimmed if the cache gets larger than "
                "this value in bytes (casdownloader flag: cache-max-size)."));
  flags.emplace_back(
      cache_lock.Flag("cas_cache_lock")
          .Help("Enable cache lock (casdownloader flag: cache-lock)."));
  flags.emplace_back(
      use_hardlink.Flag("cas_use_hardlink")
          .Help("By default local cache will use hardlink when push and pull "
                "files (casdownloader flag: use-hardlink)."));
  flags.emplace_back(
      cas_concurrency.Flag("cas_concurrency")
          .Help("the maximum number of concurrent download operations "
                "(casdownloader flag: cas-concurrency)."));
  flags.emplace_back(
      memory_limit.Flag("cas_memory_limit")
          .Help("Memory limit in MiB (casdownloader flag: memory-limit)."));
  flags.emplace_back(
      rpc_timeout.Flag("cas_rpc_timeout")
          .Help("Default RPC timeout in seconds (casdownloader flag: "
                "rpc-timeout)."));
  flags.emplace_back(
      get_capabilities_timeout.Flag("cas_get_capabilities_timeout")
          .Help("RPC timeout for GetCapabilities in seconds (casdownloader "
                "flag: get-capabilities-timeout)."));
  flags.emplace_back(
      get_tree_timeout.Flag("cas_get_tree_timeout")
          .Help("RPC timeout for GetTree in seconds "
                "(casdownloader flag: get-tree-timeout)."));
  flags.emplace_back(
      batch_read_blobs_timeout.Flag("cas_batch_read_blobs_timeout")
          .Help("RPC timeout for BatchReadBlobs in seconds (casdownloader "
                "flag: batch-read-blobs-timeout)."));
  flags.emplace_back(
      batch_update_blobs_timeout.Flag("cas_batch_update_blobs_timeout")
          .Help("RPC timeout for BatchUpdateBlobs in seconds (casdownloader "
                "flag: batch-update-blobs-timeout)."));
  flags.emplace_back(
      version.Flag("version")
          .Help("Print CAS downloader version information "
                "(casdownloader flag: version)."));

  return flags;
}

}  // namespace cuttlefish
