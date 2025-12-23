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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <json/value.h>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/cas/cas_flags.h"
#include "cuttlefish/result/result.h"

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
//   CF_EXPECT(casClient->DownloadFile(device_build, artifact_name,
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
      const DeviceBuild& build, const std::string& artifact_name,
      const std::string& target_directory,
      const DigestsFetcher& digests_fetcher,
      const std::optional<std::string>& stats_filepath = std::nullopt);

 private:
  static Result<std::unique_ptr<CasDownloader>> CreateImpl(
      const CasDownloaderFlags& cas_downloader_flags,
      const std::string& service_account_filepath);

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
