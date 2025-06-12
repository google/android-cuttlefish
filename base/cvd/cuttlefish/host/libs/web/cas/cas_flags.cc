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

#include "cuttlefish/common/libs/utils/flag_parser.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {
namespace {

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

}  // namespace

std::vector<Flag> CasDownloaderFlags::Flags() {
  std::vector<Flag> flags;

  flags.emplace_back(
      GflagsCompatFlag("cas_config_filepath", this->cas_config_filepath)
          .Help("Path to the CAS downloader config file. Other "
                "CAS flags will be ignored if this is set."));
  flags.emplace_back(
      GflagsCompatFlag("cas_downloader_path", this->downloader_path)
          .Help("Path to the CAS downloader binary. Enables CAS downloading if "
                "specified."));
  flags.emplace_back(
      GflagsCompatFlag("cas_prefer_uncompressed", this->prefer_uncompressed)
          .Help("Download uncompressed artifacts if available."));
  flags.emplace_back(
      GflagsCompatFlag("cas_cache_dir", this->cache_dir)
          .Help("Cache directory to store downloaded files (casdownloader "
                "flag: cache-dir)."));
  flags.emplace_back(
      GflagsCompatFlagInt64("cas_cache_max_size", this->cache_max_size)
          .Help("Cache is trimmed if the cache gets larger than "
                "this value in bytes (casdownloader flag: cache-max-size)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_cache_lock", this->use_hardlink)
          .Help("Enable cache lock (casdownloader flag: cache-lock)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_use_hardlink", this->use_hardlink)
          .Help("By default local cache will use hardlink when push and pull "
                "files (casdownloader flag: use-hardlink)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_concurrency", this->cas_concurrency)
          .Help("the maximum number of concurrent download operations "
                "(casdownloader flag: cas-concurrency)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_memory_limit", this->memory_limit)
          .Help("Memory limit in MiB (casdownloader flag: memory-limit)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_rpc_timeout", this->rpc_timeout)
          .Help("Default RPC timeout in seconds (casdownloader flag: "
                "rpc-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_get_capabilities_timeout",
                       this->get_capabilities_timeout)
          .Help("RPC timeout for GetCapabilities in seconds (casdownloader "
                "flag: get-capabilities-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_get_tree_timeout", this->get_tree_timeout)
          .Help("RPC timeout for GetTree in seconds "
                "(casdownloader flag: get-tree-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_batch_read_blobs_timeout",
                       this->batch_read_blobs_timeout)
          .Help("RPC timeout for BatchReadBlobs in seconds (casdownloader "
                "flag: batch-read-blobs-timeout)."));
  flags.emplace_back(
      GflagsCompatFlag("cas_batch_update_blobs_timeout",
                       this->batch_update_blobs_timeout)
          .Help("RPC timeout for BatchUpdateBlobs in seconds (casdownloader "
                "flag: batch-update-blobs-timeout)."));
  flags.emplace_back(GflagsCompatFlag("version", this->version)
                         .Help("Print CAS downloader version information "
                               "(casdownloader flag: version)."));

  return flags;
}

}  // namespace cuttlefish
