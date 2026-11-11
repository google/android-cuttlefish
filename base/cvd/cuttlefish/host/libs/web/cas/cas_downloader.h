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

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "json/value.h"

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string/device_build_string.h"
#include "cuttlefish/host/libs/web/build_api.h"
#include "cuttlefish/host/libs/web/cas/cas_flags.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
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

// The name of the file, published with each Android Build build, that lists
// the CAS digests of the build's artifacts.
inline constexpr char kCasDigestsFilename[] = "cas_digests.json";

// Downloads the artifacts of Android Build builds from CAS, as a C++ wrapper
// for the CAS downloader binary.
//
// Only the "-img-" artifacts of a build are downloaded through CAS, and only
// as whole artifacts: every other request fails. It is meant to go ahead of an
// API that serves the rest, such as the Android Build API. The digests of a
// build's artifacts are read from its cas_digests.json, which `digests_source`
// downloads.
//
// Example:
//   std::unique_ptr<CasDownloader> cas_downloader =
//       CF_EXPECT(CasDownloader::Create(cas_downloader_flags,
//                                       service_account_filepath,
//                                       android_build_api));
//   FallbackBuildApi<DeviceBuildString, DeviceBuild> build_api({
//       cas_downloader.get(),
//       &android_build_api,
//   });
//   CF_EXPECT(build_api.DownloadFile(device_build, target_dir, artifact_name));
//
class CasDownloader : public BuildApi<DeviceBuildString, DeviceBuild> {
 public:
  // `digests_source` must outlive this object.
  CasDownloader(BuildApi<DeviceBuildString, DeviceBuild>& digests_source,
                std::string downloader_path, std::vector<std::string> flags,
                bool prefer_uncompressed = false);
  static Result<std::unique_ptr<CasDownloader>> Create(
      const CasDownloaderFlags& cas_downloader_flags,
      const std::string& service_account_filepath,
      BuildApi<DeviceBuildString, DeviceBuild>& digests_source);
  ~CasDownloader() override = default;

  // CAS cannot resolve build strings; always fails.
  Result<DeviceBuild> GetBuild(const DeviceBuildString& build_string) override;

  Result<std::string> DownloadFile(const DeviceBuild& build,
                                   const std::string& target_directory,
                                   const std::string& artifact_name) override;
  // As above, and also has the CAS downloader write its download statistics
  // to `stats_filepath` when set.
  Result<std::string> DownloadFile(
      const DeviceBuild& build, const std::string& target_directory,
      const std::string& artifact_name,
      const std::optional<std::string>& stats_filepath);

  // CAS cannot serve parts of an artifact; always fails.
  Result<SeekableZipSource> FileReader(
      const DeviceBuild& build, const std::string& artifact_name) override;

 private:
  static Result<std::unique_ptr<CasDownloader>> CreateImpl(
      const CasDownloaderFlags& cas_downloader_flags,
      const std::string& service_account_filepath,
      BuildApi<DeviceBuildString, DeviceBuild>& digests_source);

  Result<CasIdentifier> GetCasIdentifier(const DeviceBuild& build,
                                         const std::string& target_directory,
                                         const std::string& artifact_name);

  BuildApi<DeviceBuildString, DeviceBuild>& digests_source_;
  std::string downloader_path_;
  std::vector<std::string> flags_;
  bool prefer_uncompressed_;
  std::string build_desc_;  // e.g. "build_id:build_target"
  Json::Value cas_digests_;
};

}  // namespace cuttlefish
