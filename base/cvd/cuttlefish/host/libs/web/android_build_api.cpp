//
// Copyright (C) 2019 The Android Open Source Project
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

#include "cuttlefish/host/libs/web/android_build_api.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <json/value.h>

#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/android_build.h"
#include "cuttlefish/host/libs/web/android_build_string.h"
#include "cuttlefish/host/libs/web/android_build_url.h"
#include "cuttlefish/host/libs/web/cas/cas_downloader.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/http_json.h"
#include "cuttlefish/host/libs/zip/remote_zip.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"
#include "cuttlefish/host/libs/zip/zip_file.h"

namespace cuttlefish {
namespace {

bool StatusIsTerminal(const std::string& status) {
  const static std::set<std::string> terminal_statuses = {
      "abandoned", "complete", "error", "ABANDONED", "COMPLETE", "ERROR",
  };
  return terminal_statuses.count(status) > 0;
}

struct CloseDir {
  void operator()(DIR* dir) { closedir(dir); }
};

Result<Json::Value> GetResponseJson(const HttpResponse<Json::Value>& response,
                                    const bool allow_redirect = false) {
  //  debug information in error responses floods stderr with too much text
  //  logged at a level that still ends up in the log file
  LOG(DEBUG) << "API response data:\n" << response.data;
  const bool response_code_allowed =
      response.HttpSuccess() || (allow_redirect && response.HttpRedirect());
  CF_EXPECTF(std::move(response_code_allowed),
             "Error response from Android Build API - {}:{}\nCheck log file "
             "for full response",
             response.http_code, response.StatusDescription());
  CF_EXPECT(!response.data.isMember("error"),
            "Response was successful, but contains error information.  Check "
            "log file for full response.");
  return response.data;
}

}  // namespace

DeviceBuild::DeviceBuild(std::string id, std::string target,
                         std::optional<std::string> filepath)
    : id(std::move(id)),
      target(std::move(target)),
      filepath(std::move(filepath)) {}

std::ostream& operator<<(std::ostream& out, const DeviceBuild& build) {
  return out << "(id=\"" << build.id << "\", target=\"" << build.target
             << "\", filepath=\"" << build.filepath.value_or("") << "\")";
}

DirectoryBuild::DirectoryBuild(std::vector<std::string> paths,
                               std::string target,
                               std::optional<std::string> filepath)
    : paths(std::move(paths)),
      target(std::move(target)),
      id("eng"),
      filepath(std::move(filepath)) {
  product = StringFromEnv("TARGET_PRODUCT", "");
}

std::ostream& operator<<(std::ostream& out, const DirectoryBuild& build) {
  auto paths = android::base::Join(build.paths, ":");
  return out << "(paths=\"" << paths << "\", target=\"" << build.target
             << "\", filepath=\"" << build.filepath.value_or("") << "\")";
}

std::ostream& operator<<(std::ostream& out, const Build& build) {
  std::visit([&out](auto&& arg) { out << arg; }, build);
  return out;
}

AndroidBuildApi::AndroidBuildApi(HttpClient& http_client,
                                 CredentialSource* credential_source,
                                 AndroidBuildUrl* android_build_url,
                                 const std::chrono::seconds retry_period,
                                 CasDownloader* cas_downloader)
    : http_client(http_client),
      credential_source(credential_source),
      android_build_url_(android_build_url),
      retry_period_(retry_period),
      cas_downloader_(cas_downloader) {}

Result<Build> AndroidBuildApi::GetBuild(const DeviceBuildString& build_string) {
  CF_EXPECT(build_string.target.has_value());
  DeviceBuild proposed_build = DeviceBuild(
      build_string.branch_or_id, *build_string.target, build_string.filepath);
  auto latest_build_id =
      CF_EXPECT(LatestBuildId(build_string.branch_or_id, *build_string.target));
  if (latest_build_id) {
    proposed_build.id = *latest_build_id;
    LOG(INFO) << "Latest build id for branch '" << build_string.branch_or_id
              << "' and target '" << proposed_build.target << "' is '"
              << proposed_build.id << "'";
  }

  std::string status = CF_EXPECT(BuildStatus(proposed_build));
  CF_EXPECT(!status.empty(),
            proposed_build << " is not a valid branch or build id.");
  LOG(DEBUG) << "Status for build " << proposed_build << " is " << status;
  while (retry_period_ != std::chrono::seconds::zero() &&
         !StatusIsTerminal(status)) {
    LOG(DEBUG) << "Status is \"" << status << "\". Waiting for "
              << retry_period_.count() << " seconds.";
    std::this_thread::sleep_for(retry_period_);
    status = CF_EXPECT(BuildStatus(proposed_build));
  }
  LOG(DEBUG) << "Status for build " << proposed_build << " is " << status;
  proposed_build.product = CF_EXPECT(ProductName(proposed_build));
  return proposed_build;
}

Result<Build> AndroidBuildApi::GetBuild(
    const DirectoryBuildString& build_string) {
  return DirectoryBuild(build_string.paths, build_string.target,
                        build_string.filepath);
}

Result<Build> AndroidBuildApi::GetBuild(const BuildString& build_string) {
  Result<Build> result =
      std::visit([this](auto&& arg) { return GetBuild(arg); }, build_string);
  return CF_EXPECT(std::move(result));
}

Result<std::string> AndroidBuildApi::DownloadFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::unordered_set<std::string> artifacts =
      CF_EXPECT(Artifacts(build, {artifact_name}));
  CF_EXPECT(Contains(artifacts, artifact_name),
            "Target " << build << " did not contain " << artifact_name);
  return DownloadTargetFile(build, target_directory, artifact_name);
}

Result<std::string> AndroidBuildApi::DownloadFileWithBackup(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name, const std::string& backup_artifact_name) {
  std::unordered_set<std::string> artifacts =
      CF_EXPECT(Artifacts(build, {artifact_name, backup_artifact_name}));
  std::string selected_artifact = artifact_name;
  if (!Contains(artifacts, artifact_name)) {
    selected_artifact = backup_artifact_name;
  }
  return DownloadTargetFile(build, target_directory, selected_artifact);
}

Result<ReadableZip> AndroidBuildApi::OpenZipArchive(
    const Build& build, const std::string& archive_name) {
  Result<ReadableZip> res =
      std::visit([this, &archive_name](
                     auto&& arg) { return OpenZipArchive(arg, archive_name); },
                 build);
  return CF_EXPECT(std::move(res));
}

Result<ReadableZip> AndroidBuildApi::OpenZipArchive(
    const DeviceBuild& build, const std::string& archive_name) {
  std::string url = CF_EXPECT(GetArtifactDownloadUrl(build, archive_name));
  std::vector<std::string> headers = CF_EXPECT(Headers());
  return CF_EXPECT(ZipFromUrl(http_client, url, headers));
}

Result<ReadableZip> AndroidBuildApi::OpenZipArchive(
    const DirectoryBuild& build, const std::string& archive_name) {
  for (const std::string& path : build.paths) {
    std::string zip_path_attempt = path + "/" + archive_name;
    if (FileExists(zip_path_attempt)) {
      return CF_EXPECT(ZipOpenRead(zip_path_attempt));
    }
  }
  return CF_ERRF("Could not find '{}'", archive_name);
}

Result<std::vector<std::string>> AndroidBuildApi::Headers() {
  std::vector<std::string> headers;
  if (credential_source) {
    headers.push_back("Authorization: Bearer " +
                      CF_EXPECT(credential_source->Credential()));
  }
  return headers;
}

Result<std::optional<std::string>> AndroidBuildApi::LatestBuildId(
    const std::string& branch, const std::string& target) {
  const std::string url =
      android_build_url_->GetLatestBuildIdUrl(branch, target);
  auto response =
      CF_EXPECT(HttpGetToJson(http_client, url, CF_EXPECT(Headers())));

  const Json::Value json = CF_EXPECTF(GetResponseJson(response),
                                      "Error fetching last known good build "
                                      "id for:\nbranch \"{}\", target \"{}\"",
                                      branch, target);
  if (!json.isMember("builds")) {
    return std::nullopt;
  }

  CF_EXPECTF(json["builds"].isArray() && json["builds"].size() == 1,
             "Expected to find a single latest build for branch \"{}\" and "
             "target \"{}\" in the response array, "
             "but found {}",
             branch, target, json["builds"].size());
  return CF_EXPECT(GetValue<std::string>(json["builds"][0], { "buildId" }));
}

Result<std::string> AndroidBuildApi::BuildStatus(const DeviceBuild& build) {
  const std::string url =
      android_build_url_->GetBuildStatusUrl(build.id, build.target);
  auto response =
      CF_EXPECT(HttpGetToJson(http_client, url, CF_EXPECT(Headers())));

  std::string no_auth_error_message;
  if (credential_source == nullptr && response.http_code == 404) {
    // In LatestBuildId we currently cannot distinguish between the cases:
    //    - user provided a build ID (not an error)
    //    - user provided a branch with a typo
    //    - user provided a branch without the necessary authentication
    //    (for example, internal branches)
    // This message is a best attempt at helping the user in third case
    no_auth_error_message =
        "\n\nThis fetch was run unauthenticated, which could be the "
        "problem.\nTry `cvd help login`";
  }
  const Json::Value json = CF_EXPECT(
      GetResponseJson(response),
      "Error fetching build status for build:\n"
          << build
          << "\n\nIf you specified a branch and it appears in the build id "
             "field of this error, there was a problem retrieving the latest "
             "build id.\n\nIs there a typo in the branch or target name?"
          << no_auth_error_message);

  return CF_EXPECT(GetValue<std::string>(json, { "buildAttemptStatus" }));
}

Result<std::string> AndroidBuildApi::ProductName(const DeviceBuild& build) {
  const std::string url =
      android_build_url_->GetProductNameUrl(build.id, build.target);
  auto response =
      CF_EXPECT(HttpGetToJson(http_client, url, CF_EXPECT(Headers())));
  const Json::Value json = CF_EXPECT(GetResponseJson(response),
                                     "Error fetching product name for build:\n"
                                         << build);
  return CF_EXPECT(GetValue<std::string>(json, { "target", "product" }));
}

Result<std::unordered_set<std::string>> AndroidBuildApi::Artifacts(
    const DeviceBuild& build,
    const std::vector<std::string>& artifact_filenames) {
  std::string page_token = "";
  std::unordered_set<std::string> artifacts;

  do {
    const std::string url = android_build_url_->GetArtifactUrl(
        build.id, build.target, artifact_filenames, page_token);
    auto response =
        CF_EXPECT(HttpGetToJson(http_client, url, CF_EXPECT(Headers())));

    const Json::Value json = CF_EXPECT(GetResponseJson(response),
                                       "Error fetching artifacts list for:\n"
                                           << build);
    for (const auto& artifact_json : json["artifacts"]) {
      artifacts.emplace(
          CF_EXPECT(GetValue<std::string>(artifact_json, {"name"})));
    }

    if (json.isMember("nextPageToken")) {
      page_token = json["nextPageToken"].asString();
    } else {
      page_token = "";
    }
  } while (!page_token.empty());

  return artifacts;
}

Result<std::unordered_set<std::string>> AndroidBuildApi::Artifacts(
    const DirectoryBuild& build, const std::vector<std::string>&) {
  std::unordered_set<std::string> artifacts;
  for (const auto& path : build.paths) {
    auto dir = std::unique_ptr<DIR, CloseDir>(opendir(path.c_str()));
    CF_EXPECT(dir != nullptr, "Could not read files from \"" << path << "\"");
    for (auto entity = readdir(dir.get()); entity != nullptr;
         entity = readdir(dir.get())) {
      artifacts.emplace(std::string(entity->d_name));
    }
  }
  return artifacts;
}

Result<std::unordered_set<std::string>> AndroidBuildApi::Artifacts(
    const Build& build, const std::vector<std::string>& artifact_filenames) {
  auto res =
      std::visit([this, &artifact_filenames](
                     auto&& arg) { return Artifacts(arg, artifact_filenames); },
                 build);
  return CF_EXPECT(std::move(res));
}

Result<std::string> AndroidBuildApi::GetArtifactDownloadUrl(
    const DeviceBuild& build, const std::string& artifact) {
  const std::string download_url_endpoint =
      android_build_url_->GetArtifactDownloadUrl(build.id, build.target,
                                                 artifact);
  auto response = CF_EXPECT(
      HttpGetToJson(http_client, download_url_endpoint, CF_EXPECT(Headers())));
  const Json::Value json =
      CF_EXPECTF(GetResponseJson(response, /* allow redirect response */ true),
                 "Error fetching download URL for \"{}\" from build ID \"{}\"",
                 artifact, build.id);
  return CF_EXPECT(GetValue<std::string>(json, { "signedUrl" }));
}

Result<void> AndroidBuildApi::ArtifactToFile(const DeviceBuild& build,
                                             const std::string& artifact,
                                             const std::string& path) {
  const auto url = CF_EXPECT(GetArtifactDownloadUrl(build, artifact));
  auto response = CF_EXPECT(HttpGetToFile(http_client, url, path));
  CF_EXPECTF(response.HttpSuccess(), "Failed to download file: {}",
             response.StatusDescription());
  return {};
}

Result<void> AndroidBuildApi::ArtifactToFile(const DirectoryBuild& build,
                                             const std::string& artifact,
                                             const std::string& path) {
  for (const auto& path : build.paths) {
    auto source = path + "/" + artifact;
    if (!FileExists(source)) {
      continue;
    }
    unlink(path.c_str());
    CF_EXPECT(symlink(source.c_str(), path.c_str()) == 0,
              "Could not create symlink from " << source << " to " << path
                                               << ": " << strerror(errno));
    return {};
  }
  return CF_ERR("Could not find artifact \"" << artifact << "\" in build \""
                                             << build << "\"");
}

Result<void> AndroidBuildApi::ArtifactToFile(const Build& build,
                                             const std::string& artifact,
                                             const std::string& path) {
  auto res = std::visit(
      [this, &artifact, &path](auto&& arg) {
        return ArtifactToFile(arg, artifact, path);
      },
      build);
  CF_EXPECT(std::move(res));
  return {};
}

Result<std::string> AndroidBuildApi::DownloadTargetFileFromCas(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  CF_EXPECT(cas_downloader_ != nullptr, "CAS downloading is not enabled.");
  CF_EXPECT(std::holds_alternative<DeviceBuild>(build),
            "CAS downloading is only supported for DeviceBuild.");
  std::tuple<std::string, std::string> id_target = GetBuildIdAndTarget(build);
  std::string build_id = std::get<0>(id_target);
  std::string build_target = std::get<1>(id_target);
  LOG(INFO) << "Download from CAS: '" << artifact_name << "'";
  std::string target_filepath =
      ConstructTargetFilepath(target_directory, artifact_name);
  DigestsFetcher digests_fetcher =
      [&build, &target_directory,
       this](std::string filename) -> Result<std::string> {
    CF_EXPECTF(DownloadFile(build, target_directory, filename),
               "Failed to download '{}' from AB.", filename);
    return ConstructTargetFilepath(target_directory, filename);
  };
  CF_EXPECT(cas_downloader_->DownloadFile(build_id, build_target, artifact_name,
                                          target_directory, digests_fetcher));

  return {target_filepath};
}

Result<std::string> AndroidBuildApi::DownloadTargetFile(
    const Build& build, const std::string& target_directory,
    const std::string& artifact_name) {
  std::string target_filepath =
      ConstructTargetFilepath(target_directory, artifact_name);
  if (cas_downloader_ != nullptr &&
      std::holds_alternative<DeviceBuild>(build) &&
      artifact_name.find("-img-") != std::string::npos) {
    Result<std::string> result =
        DownloadTargetFileFromCas(build, target_directory, artifact_name);
    if (result.ok()) {
      return {target_filepath};
    }
    // Fallback to download from AB.
  }
  CF_EXPECT(ArtifactToFile(build, artifact_name, target_filepath),
            "Unable to download " << build << ":" << artifact_name << " to "
                                  << target_filepath);
  return {target_filepath};
}

std::tuple<std::string, std::string> GetBuildIdAndTarget(const Build& build) {
  auto id = std::visit([](auto&& arg) { return arg.id; }, build);
  auto target = std::visit([](auto&& arg) { return arg.target; }, build);
  return {id, target};
}

std::optional<std::string> GetFilepath(const Build& build) {
  return std::visit([](auto&& arg) { return arg.filepath; }, build);
}

std::string ConstructTargetFilepath(const std::string& directory,
                                    const std::string& filename) {
  return directory + "/" + filename;
}

}  // namespace cuttlefish
