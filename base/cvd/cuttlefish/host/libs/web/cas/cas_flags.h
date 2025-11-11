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

#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/flag_parser.h"

namespace cuttlefish {

inline constexpr int kDefaultMemoryLimit = 0;  // 0 for no limit.
inline constexpr int kDefaultCasConcurrency = 500;
inline constexpr int kDefaultRpcTimeout = 120;
inline constexpr int kDefaultGetCapabilitiesTimeout = 5;
inline constexpr int kDefaultGetTreeTimeout = 5;
inline constexpr int kDefaultBatchReadBlobsTimeout = 180;
inline constexpr int kDefaultBatchUpdateBlobsTimeout = 60;

// Default cache size for CAS cache: 8 GiB.
// Note: this is only effective when cache_dir is set.
inline constexpr int64_t kMinCacheMaxSize = 8LL * 1024 * 1024 * 1024;

// Flags for the CAS downloader binary.
struct CasDownloaderFlags {
  std::vector<Flag> Flags();

  std::string cas_config_filepath = "";
  std::string downloader_path = "";
  bool prefer_uncompressed = false;
  std::string cache_dir = "";
  int64_t cache_max_size = kMinCacheMaxSize;  // Only effective when cache_dir is set.
  bool cache_lock = false;
  bool use_hardlink = true;
  int memory_limit = kDefaultMemoryLimit;
  int cas_concurrency = kDefaultCasConcurrency;
  int rpc_timeout = kDefaultRpcTimeout;
  int get_capabilities_timeout = kDefaultGetCapabilitiesTimeout;
  int get_tree_timeout = kDefaultGetTreeTimeout;
  int batch_read_blobs_timeout = kDefaultBatchReadBlobsTimeout;
  int batch_update_blobs_timeout = kDefaultBatchUpdateBlobsTimeout;
  bool version = false;
};

}  // namespace cuttlefish
