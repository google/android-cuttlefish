//
// Copyright (C) 2025 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/fetch/downloaders.h"

#include <memory>
#include <string>

#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/fetch/fetch_cvd_parser.h"
#include "cuttlefish/host/commands/cvd/utils/common.h"
#include "cuttlefish/host/libs/web/caching_build_api.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/luci_build_api.h"
#include "cuttlefish/host/libs/web/oauth2_consent.h"

namespace cuttlefish {
namespace {

Result<std::unique_ptr<CredentialSource>> GetCredentialSourceFromFlags(
    HttpClient& http_client, const BuildApiFlags& flags,
    const std::string& oauth_filepath) {
  return CF_EXPECT(
      GetCredentialSource(http_client, flags.credential_source, oauth_filepath,
                          flags.credential_flags.use_gce_metadata,
                          flags.credential_flags.credential_filepath,
                          flags.credential_flags.service_account_filepath));
}

}  // namespace

struct Downloaders::Impl {
  std::unique_ptr<HttpClient> curl_;
  std::unique_ptr<HttpClient> retrying_http_client_;
  std::unique_ptr<CredentialSource> android_creds_;
  std::unique_ptr<CasDownloader> cas_downloader_;
  std::unique_ptr<AndroidBuildApi> android_build_api_;
  std::unique_ptr<CachingBuildApi> caching_build_api_;
  std::unique_ptr<CredentialSource> luci_credential_source_;
  std::unique_ptr<CredentialSource> gsutil_credential_source_;
  std::unique_ptr<LuciBuildApi> luci_build_api_;
};

Downloaders::Downloaders(std::unique_ptr<Downloaders::Impl> impl)
    : impl_(std::move(impl)) {}
Downloaders::Downloaders(Downloaders&&) = default;
Downloaders::~Downloaders() = default;

Result<Downloaders> Downloaders::Create(const BuildApiFlags& flags) {
  std::unique_ptr<Downloaders::Impl> impl(new Downloaders::Impl());

  const bool use_logging_debug_function = true;
  impl->curl_ = HttpClient::CurlClient(use_logging_debug_function);
  impl->retrying_http_client_ = HttpClient::ServerErrorRetryClient(
      *impl->curl_, 10, std::chrono::milliseconds(5000));

  std::vector<std::string> scopes = {
      kAndroidBuildApiScope,
      "https://www.googleapis.com/auth/userinfo.email",
  };
  Result<std::unique_ptr<CredentialSource>> cvd_creds =
      CredentialForScopes(*impl->curl_, scopes);

  std::string oauth_filepath =
      StringFromEnv("HOME", ".") + "/.acloud_oauth2.dat";

  impl->android_creds_ =
      cvd_creds.ok() && cvd_creds->get()
          ? std::move(*cvd_creds)
          : CF_EXPECT(GetCredentialSourceFromFlags(*impl->retrying_http_client_,
                                                   flags, oauth_filepath));

  Result<std::unique_ptr<CasDownloader>> cas_downloader_result =
      CasDownloader::Create(flags.cas_downloader_flags,
                            flags.credential_flags.service_account_filepath);
  if (cas_downloader_result.ok()) {
    impl->cas_downloader_ = std::move(cas_downloader_result.value());
  }

  impl->android_build_api_ = std::make_unique<AndroidBuildApi>(
      *impl->retrying_http_client_, impl->android_creds_.get(), flags.api_key,
      flags.wait_retry_period, flags.api_base_url, flags.project_id,
      impl->cas_downloader_.get());

  const std::string cache_base_path = PerUserCacheDir();
  if (flags.enable_caching && EnsureCacheDirectory(cache_base_path)) {
    impl->caching_build_api_ = std::make_unique<CachingBuildApi>(
        *impl->android_build_api_, cache_base_path);
  }

  impl->luci_credential_source_ = CF_EXPECT(GetCredentialSourceFromFlags(
      *impl->retrying_http_client_, flags,
      StringFromEnv("HOME", ".") + "/.config/chrome_infra/auth/tokens.json"));
  impl->gsutil_credential_source_ = CF_EXPECT(
      GetCredentialSourceFromFlags(*impl->retrying_http_client_, flags,
                                   StringFromEnv("HOME", ".") + "/.boto"));

  impl->luci_build_api_ = std::make_unique<LuciBuildApi>(
      *impl->retrying_http_client_, impl->luci_credential_source_.get(),
      impl->gsutil_credential_source_.get());

  return Downloaders(std::move(impl));
}

BuildApi& Downloaders::AndroidBuild() {
  if (impl_->caching_build_api_) {
    return *impl_->caching_build_api_;
  } else {
    return *impl_->android_build_api_;
  }
}

LuciBuildApi& Downloaders::Luci() { return *impl_->luci_build_api_; }

}  // namespace cuttlefish
