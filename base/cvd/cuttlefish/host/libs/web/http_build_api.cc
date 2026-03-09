//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/http_build_api.h"

#include <string>

#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/remote_zip.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

HttpBuildApi::HttpBuildApi(HttpClient& http_client)
    : http_client_(http_client) {}

Result<Build> HttpBuildApi::GetBuild(const HttpBuildString& build_string) {
  return HttpBuild{
      .url = build_string.url,
      .filepath = build_string.filepath,
  };
}

Result<std::string> HttpBuildApi::DownloadFile(
    const HttpBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::string dest_path = target_directory + "/" + artifact_name;
  auto response = CF_EXPECT(HttpGetToFile(http_client_, build.url, dest_path));
  CF_EXPECT(response.HttpSuccess(),
            "Failed to download " << build.url << ", HTTP status: "
                                  << response.http_code << " ("
                                  << response.StatusDescription() << ")");
  return dest_path;
}

Result<SeekableZipSource> HttpBuildApi::FileReader(const HttpBuild& build,
                                                   const std::string&) {
  return CF_EXPECT(ZipSourceFromUrl(http_client_, build.url, /*headers=*/{}));
}

}  // namespace cuttlefish
