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

#include "cuttlefish/host/commands/cvd/fetch/build_api_credentials.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/commands/cvd/fetch/build_api_flags.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

std::unique_ptr<CredentialSource> TryParseServiceAccount(
    HttpClient& http_client, const std::string& file_content) {
  Json::Reader reader;
  Json::Value content;
  if (!reader.parse(file_content, content)) {
    // Don't log the actual content of the file since it could be the actual
    // access token.
    VLOG(0) << "Could not parse credential file as Service Account";
    return {};
  }
  auto result = ServiceAccountOauthCredentialSource::FromJson(
      http_client, content, kAndroidBuildApiScope);
  if (!result.ok()) {
    VLOG(0) << "Failed to load service account json file: \n" << result.error();
    return {};
  }
  return std::move(*result);
}

Result<std::unique_ptr<CredentialSource>> GetCredentialSourceLegacy(
    HttpClient& http_client, const std::string& credential_source,
    const std::string& oauth_filepath) {
  std::unique_ptr<CredentialSource> result;
  if (credential_source == "gce") {
    result = GceMetadataCredentialSource::Make(http_client);
  } else if (credential_source.empty()) {
    if (FileExists(oauth_filepath)) {
      std::string oauth_contents = CF_EXPECT(ReadFileContents(oauth_filepath));
      auto attempt_load = RefreshTokenCredentialSource::FromOauth2ClientFile(
          http_client, oauth_contents);
      if (attempt_load.ok()) {
        result = std::move(*attempt_load);
        VLOG(0) << "Loaded credentials from '" << oauth_filepath << "'";
      } else {
        LOG(ERROR) << "Failed to load oauth credentials from \""
                   << oauth_filepath << "\":" << attempt_load.error();
      }
    } else {
      LOG(INFO) << "\"" << oauth_filepath
                << "\" is missing, running without credentials";
    }
  } else if (!FileExists(credential_source)) {
    // If the parameter doesn't point to an existing file it must be the
    // credentials.
    result = FixedCredentialSource::Make(credential_source);
  } else {
    // Read the file only once in case it's a pipe.
    VLOG(0) << "Attempting to open credentials file \"" << credential_source
            << "\"";
    auto file_content =
        CF_EXPECTF(ReadFileContents(credential_source),
                   "Failure getting credential file contents from file \"{}\"",
                   credential_source);
    if (auto crds = TryParseServiceAccount(http_client, file_content)) {
      result = std::move(crds);
    } else {
      result = FixedCredentialSource::Make(file_content);
    }
  }
  return result;
}

Result<std::unique_ptr<CredentialSource>> GetCredentialSource(
    HttpClient& http_client, const std::string& credential_source,
    const std::string& oauth_filepath, const bool use_gce_metadata,
    const std::string& credential_filepath,
    const std::string& service_account_filepath) {
  const int number_of_set_credentials =
      !credential_source.empty() + use_gce_metadata +
      !credential_filepath.empty() + !service_account_filepath.empty();
  CF_EXPECT_LE(number_of_set_credentials, 1,
               "At most a single credential option may be used.");

  if (use_gce_metadata) {
    return GceMetadataCredentialSource::Make(http_client);
  }
  if (!credential_filepath.empty()) {
    std::string contents =
        CF_EXPECTF(ReadFileContents(credential_filepath),
                   "Failure getting credential file contents from file \"{}\".",
                   credential_filepath);
    return FixedCredentialSource::Make(contents);
  }
  if (!service_account_filepath.empty()) {
    std::string contents =
        CF_EXPECTF(ReadFileContents(service_account_filepath),
                   "Failure getting service account credential file contents "
                   "from file \"{}\".",
                   service_account_filepath);
    auto service_account_credentials =
        TryParseServiceAccount(http_client, contents);
    CF_EXPECTF(service_account_credentials != nullptr,
               "Unable to parse service account credentials in file \"{}\".  "
               "File contents: {}",
               service_account_filepath, contents);
    return std::move(service_account_credentials);
  }
  // use the deprecated credential_source or no value
  // when this helper is removed its `.acloud_oauth2.dat` processing should be
  // moved here
  return GetCredentialSourceLegacy(http_client, credential_source,
                                   oauth_filepath);
}

}  // namespace

Result<std::unique_ptr<CredentialSource>> GetCredentialSourceFromFlags(
    HttpClient& http_client, const BuildApiFlags& flags,
    const std::string& oauth_filepath) {
  return CF_EXPECT(
      GetCredentialSource(http_client, flags.credential_source, oauth_filepath,
                          flags.credential_flags.use_gce_metadata,
                          flags.credential_flags.credential_filepath,
                          flags.credential_flags.service_account_filepath));
}

}  // namespace cuttlefish
