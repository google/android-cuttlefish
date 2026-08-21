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

#include <optional>
#include <string>
#include <utility>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/digest.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/http_probe.h"
#include "cuttlefish/host/libs/web/url_namespace.h"
#include "cuttlefish/host/libs/zip/buffered_zip_source.h"
#include "cuttlefish/host/libs/zip/libzip_cc/archive.h"
#include "cuttlefish/host/libs/zip/libzip_cc/seekable_source.h"
#include "cuttlefish/host/libs/zip/remote_zip.h"
#include "cuttlefish/host/libs/zip/zip_file.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<std::string> ArtifactUrl(const HttpBuild& build,
                                const std::string& artifact_name) {
  if (build.object.has_value()) {
    CF_EXPECTF(artifact_name == *build.object,
               "The build '{}' holds only '{}', so it has no '{}'.", build.id,
               *build.object, artifact_name);
    return build.url;
  }
  return build.url + artifact_name;
}

}  // namespace

HttpBuildApi::HttpBuildApi(HttpClient& http_client)
    : http_client_(http_client) {}

Result<HttpBuild> HttpBuildApi::GetBuild(const HttpBuildString& build_string) {
  HttpBuild build = CF_EXPECT(HttpBuild::FromBuildString(build_string));
  // A directory of plain HTTPS URLs has nothing to list and nothing to probe,
  // so its artifacts are only known to be absent when they answer 404.
  if (build.object.has_value()) {
    build.object_info = CF_EXPECT(ProbeHttpObject(http_client_, build.url, {}));
  }
  return build;
}

Result<std::string> HttpBuildApi::DownloadFile(
    const HttpBuild& build, const std::string& target_directory,
    const std::string& artifact_name) {
  const std::string dest_path =
      ConstructTargetFilepath(target_directory, artifact_name);
  CF_EXPECT(EnsureDirectoryExists(target_directory));

  if (IsArchiveMember(build.object, build.filepath, artifact_name)) {
    CF_EXPECTF(!!build.object_info.accept_ranges,
               "'{}' does not serve range requests, so '{}' cannot be read out "
               "of it.",
               build.id, artifact_name);
    SeekableZipSource source = CF_EXPECT(FileReader(build, *build.object));
    ReadableZip zip = CF_EXPECT(BufferAndOpenZip(std::move(source)));
    CF_EXPECTF(ExtractFile(zip, artifact_name, dest_path),
               "Could not read '{}' out of '{}'.", artifact_name, build.id);
    return dest_path;
  }

  const std::string url = CF_EXPECT(ArtifactUrl(build, artifact_name));
  HttpResponse<std::string> response =
      CF_EXPECT(HttpGetToFile(http_client_, url, dest_path));
  CF_EXPECTF(response.HttpSuccess(),
             "Could not download '{}' from '{}' - {}:{}", artifact_name,
             build.id, response.http_code, response.StatusDescription());
  if (build.sha256.has_value()) {
    CF_EXPECT(VerifySha256(dest_path, *build.sha256, artifact_name));
  }
  return dest_path;
}

Result<SeekableZipSource> HttpBuildApi::FileReader(
    const HttpBuild& build, const std::string& artifact_name) {
  const std::string url = CF_EXPECT(ArtifactUrl(build, artifact_name));
  if (build.object_info.size.has_value()) {
    return CF_EXPECT(
        ZipSourceFromUrl(http_client_, url, {}, *build.object_info.size));
  }
  // Only a directory reaches here, having had no probe to learn a size from.
  return CF_EXPECT(ZipSourceFromUrl(http_client_, url, {}));
}

}  // namespace cuttlefish
