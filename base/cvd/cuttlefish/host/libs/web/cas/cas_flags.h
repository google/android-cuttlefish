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

// Wrapper for flag values that tracks whether a value was user-specified
// (via command-line or config file) versus using a default.
template <typename T>
struct FlagValue {
  T value;
  bool user_specified = false;

  FlagValue() = default;
  explicit FlagValue(const T& default_val)
      : value(default_val), user_specified(false) {}

  // Assignment operator for setting value (preserves user_specified flag)
  FlagValue& operator=(const T& new_value) {
    value = new_value;
    return *this;
  }

  // Conversion operators for backward compatibility with code expecting T
  operator const T&() const { return value; }
  operator T&() { return value; }

  // Pointer operators
  const T* operator->() const { return &value; }
  T* operator->() { return &value; }

  // Comparison operators
  bool operator==(const T& other) const { return value == other; }
  bool operator!=(const T& other) const { return value != other; }
  bool operator<(const T& other) const { return value < other; }
  bool operator>(const T& other) const { return value > other; }
  bool operator<=(const T& other) const { return value <= other; }
  bool operator>=(const T& other) const { return value >= other; }
};

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

// Default path to CAS downloader config file.
inline constexpr const char kDefaultCasConfigFilePath[] = "/etc/casdownloader/config.json";

// Default path to CAS downloader binary.
inline constexpr const char kDefaultDownloaderPath[] = "/usr/bin/casdownloader";

// Flags for the CAS downloader binary.
struct CasDownloaderFlags {
  CasDownloaderFlags();
  std::vector<Flag> Flags();

  FlagValue<std::string> cas_config_filepath;
  FlagValue<std::string> downloader_path;
  FlagValue<bool> prefer_uncompressed;
  FlagValue<std::string> cache_dir;
  FlagValue<std::string> invocation_id;
  FlagValue<int64_t> cache_max_size;
  FlagValue<bool> cache_lock;
  FlagValue<bool> use_hardlink;
  FlagValue<int> memory_limit;
  FlagValue<int> cas_concurrency;
  FlagValue<int> rpc_timeout;
  FlagValue<int> get_capabilities_timeout;
  FlagValue<int> get_tree_timeout;
  FlagValue<int> batch_read_blobs_timeout;
  FlagValue<int> batch_update_blobs_timeout;
  FlagValue<bool> version;
};

}  // namespace cuttlefish
