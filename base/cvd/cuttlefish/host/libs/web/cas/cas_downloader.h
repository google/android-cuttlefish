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

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/libs/utils/result.h"
#include "common/libs/utils/subprocess.h"
#include "json/value.h"

namespace cuttlefish {

inline constexpr char kKeyDownloaderPath[] = "downloader-path";
inline constexpr char kKeyFlags[] = "flags";

inline constexpr char kFlagDigest[] = "digest";
inline constexpr char kFlagDir[] = "dir";
inline constexpr char kFlagDumpJson[] = "dump-json";
inline constexpr char kFlagDisableCache[] = "disable-cache";
inline constexpr char kFlagCasInstance[] = "cas-instance";
inline constexpr char kFlagCasAddr[] = "cas-addr";
inline constexpr char kFlagServiceAccountJson[] = "service-account-json";
inline constexpr char kFlagUseAdc[] = "use-adc";

inline constexpr int kDefaultMemoryLimit = 0;  // 0 for no limit.
inline constexpr int kDefaultCasConcurrency = 500;
inline constexpr int kDefaultRpcTimeout = 120;
inline constexpr int kDefaultGetCapabilitiesTimeout = 5;
inline constexpr int kDefaultGetTreeTimeout = 5;
inline constexpr int kDefaultBatchReadBlobsTimeout = 180;
inline constexpr int kDefaultBatchUpdateBlobsTimeout = 60;

// Flags for the CAS downloader binary.
struct CasDownloaderFlags {
  std::string cas_config_filepath = "";
  std::string downloader_path = "";
  bool prefer_uncompressed = false;
  std::string cache_dir = "";
  int64_t cache_max_size = 0;  // Must be > 0 if cache_dir is set.
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

// Identifies an artifact in CAS.
// The digest of an artifact is unique in a CAS instance. To identify the CAS
// instance, the cas_instance and cas_addr are required. An artifact can only be
// downloaded from the CAS instance it is uploaded to. This info is available in
// cas_digests.json from AB.
struct CasIdentifier {
  std::string cas_instance;
  std::string cas_addr;
  std::string digest;
  // The actual filename in CAS, can be different from the the artifact_name.
  std::string filename;
};

// A callback function provided by the caller of CasDownloader::DownloadFile to
// fetch digests or other artifacts not available on cas. The callback function
// takes the path of the artifact on AB and returns the local path of the
// downloaded file.
using DigestsFetcher = std::function<Result<std::string>(std::string)>;

// A c++ wrapper for the CAS downloader binary.
// Example:
//   std::unique_ptr<CasDownloader> casdownloader =
//       CF_EXPECT(CasDownloader::Create(cas_downloader_flags,
//                                       service_account_filepath),
//                 "Failed to create CasDownloader.");
//   CF_EXPECT(casClient->DownloadFile(build_id, build_target, artifact_name,
//                                     target_dir, digests_fetcher),
//             "Failed to download file from CAS.");
//
class CasDownloader {
 public:
  CasDownloader(std::string downloader_path, std::vector<std::string> flags,
                bool prefer_uncompressed = false);
  static Result<std::unique_ptr<CasDownloader>> Create(
      const CasDownloaderFlags& cas_downloader_flags,
      const std::string& service_account_filepath);
  virtual ~CasDownloader() = default;
  virtual Result<void> DownloadFile(
      const std::string& build_id, const std::string& build_target,
      const std::string& artifact_name, const std::string& target_directory,
      const DigestsFetcher& digests_fetcher,
      const std::optional<std::string>& stats_filepath = std::nullopt);

 private:
  Result<CasIdentifier> GetCasIdentifier(const std::string& build_id,
                                         const std::string& build_target,
                                         const std::string& artifact_name,
                                         const DigestsFetcher& digests_fetcher);

  std::string downloader_path_;
  std::vector<std::string> flags_;
  bool prefer_uncompressed_;
  std::string build_desc_;  // e.g. "build_id:build_target"
  Json::Value cas_digests_;
};

}  // namespace cuttlefish
