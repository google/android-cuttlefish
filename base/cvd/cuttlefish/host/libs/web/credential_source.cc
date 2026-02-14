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

#include "cuttlefish/host/libs/web/credential_source.h"

#include <stddef.h>
#include <stdint.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/strings.h>
#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>
#include <openssl/base.h>
#include <openssl/bio.h>
#include <openssl/digest.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_json.h"
#include "cuttlefish/host/libs/web/http_client/url_escape.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr auto kRefreshWindow = std::chrono::minutes(2);

// Credentials with known expiration times with behavior to load new
// credentials.
class RefreshingCredentialSource : public CredentialSource {
 public:
  RefreshingCredentialSource()
      : expiration_(std::chrono::steady_clock::time_point::min()) {}

  virtual Result<std::string> Credential() final override {
    std::lock_guard lock(latest_credential_mutex_);
    if (expiration_ + kRefreshWindow < std::chrono::steady_clock::now()) {
      const auto& [credential, expiration] = CF_EXPECT(Refresh());
      latest_credential_ = credential;
      expiration_ = std::chrono::steady_clock::now() + expiration;
    }
    return latest_credential_;
  }

 private:
  virtual Result<std::pair<std::string, std::chrono::seconds>> Refresh() = 0;

  std::string latest_credential_;
  std::mutex latest_credential_mutex_;
  std::chrono::steady_clock::time_point expiration_;
};

// OAuth2 credentials from the GCE metadata server.
//
// -
// https://cloud.google.com/compute/docs/access/authenticate-workloads#applications
// - https://cloud.google.com/compute/docs/metadata/overview
class GceMetadataCredentialSource : public RefreshingCredentialSource {
 public:
  GceMetadataCredentialSource(HttpClient&);

  static std::unique_ptr<CredentialSource> Make(HttpClient&);

 private:
  Result<std::pair<std::string, std::chrono::seconds>> Refresh() override;

  HttpClient& http_client_;
};

// Pass through a string as an authentication token with unknown expiration.
class FixedCredentialSource : public CredentialSource {
 public:
  FixedCredentialSource(const std::string& credential);

  Result<std::string> Credential() override;

  static std::unique_ptr<CredentialSource> Make(const std::string& credential);

 private:
  std::string credential_;
};

// OAuth2 tokens from a desktop refresh token.
//
// https://developers.google.com/identity/protocols/oauth2/native-app
class RefreshTokenCredentialSource : public RefreshingCredentialSource {
 public:
  static Result<std::unique_ptr<RefreshTokenCredentialSource>>
  FromOauth2ClientFile(HttpClient& http_client,
                       const std::string& oauth_contents);

  RefreshTokenCredentialSource(HttpClient& http_client,
                               const std::string& client_id,
                               const std::string& client_secret,
                               const std::string& refresh_token);

 private:
  static Result<std::unique_ptr<RefreshTokenCredentialSource>> FromJson(
      HttpClient& http_client, const Json::Value& credential);

  Result<std::pair<std::string, std::chrono::seconds>> Refresh() override;

  HttpClient& http_client_;
  std::string client_id_;
  std::string client_secret_;
  std::string refresh_token_;
};

// OAuth2 tokens from service account files.
//
// https://developers.google.com/identity/protocols/oauth2/service-account
class ServiceAccountOauthCredentialSource : public RefreshingCredentialSource {
 public:
  static Result<std::unique_ptr<ServiceAccountOauthCredentialSource>> FromJson(
      HttpClient& http_client, const Json::Value& service_account_json,
      const std::string& scope);

 private:
  ServiceAccountOauthCredentialSource(HttpClient& http_client);

  Result<std::pair<std::string, std::chrono::seconds>> Refresh() override;

  HttpClient& http_client_;
  std::string email_;
  std::string scope_;
  std::unique_ptr<EVP_PKEY, void (*)(EVP_PKEY*)> private_key_;
};

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

GceMetadataCredentialSource::GceMetadataCredentialSource(
    HttpClient& http_client)
    : http_client_(http_client) {}

Result<std::pair<std::string, std::chrono::seconds>>
GceMetadataCredentialSource::Refresh() {
  static constexpr char kRefreshUrl[] =
      "http://metadata.google.internal/computeMetadata/v1/instance/"
      "service-accounts/default/token";
  auto response = CF_EXPECT(
      HttpGetToJson(http_client_, kRefreshUrl, {"Metadata-Flavor: Google"}));
  const auto& json = response.data;
  CF_EXPECT(response.HttpSuccess(),
            "Error fetching credentials. The server response was \""
                << json << "\", and code was " << response.http_code);
  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  CF_EXPECT(json.isMember("access_token") && json.isMember("expires_in"),
            "GCE credential was missing access_token or expires_in. "
                << "Full response was " << json << "");

  return {{json["access_token"].asString(),
           std::chrono::seconds(json["expires_in"].asInt())}};
}

std::unique_ptr<CredentialSource> GceMetadataCredentialSource::Make(
    HttpClient& http_client) {
  return std::unique_ptr<CredentialSource>(
      new GceMetadataCredentialSource(http_client));
}

FixedCredentialSource::FixedCredentialSource(const std::string& credential) {
  this->credential_ = credential;
}

Result<std::string> FixedCredentialSource::Credential() { return credential_; }

std::unique_ptr<CredentialSource> FixedCredentialSource::Make(
    const std::string& credential) {
  return std::unique_ptr<CredentialSource>(new FixedCredentialSource(credential));
}

Result<std::unique_ptr<RefreshTokenCredentialSource>>
RefreshTokenCredentialSource::FromJson(HttpClient& http_client,
                                       const Json::Value& credential) {
  CF_EXPECT(credential.isMember("client_id"));
  auto& client_id = credential["client_id"];
  CF_EXPECT(client_id.type() == Json::ValueType::stringValue);

  CF_EXPECT(credential.isMember("client_secret"));
  auto& client_secret = credential["client_secret"];
  CF_EXPECT(client_secret.type() == Json::ValueType::stringValue);

  CF_EXPECT(credential.isMember("refresh_token"));
  auto& refresh_token = credential["refresh_token"];
  CF_EXPECT(refresh_token.type() == Json::ValueType::stringValue);

  return std::make_unique<RefreshTokenCredentialSource>(
      http_client, client_id.asString(), client_secret.asString(),
      refresh_token.asString());
}

Result<std::unique_ptr<RefreshTokenCredentialSource>>
RefreshTokenCredentialSource::FromOauth2ClientFile(
    HttpClient& http_client, const std::string& oauth_contents) {
  if (absl::StartsWith(oauth_contents, "[OAuth2]")) {  // .boto file
    std::optional<std::string> client_id;
    std::optional<std::string> client_secret;
    std::optional<std::string> refresh_token;
    auto lines = android::base::Tokenize(oauth_contents, "\n");
    for (auto line : lines) {
      std::string_view line_view = line;
      static constexpr std::string_view kClientIdPrefix = "client_id =";
      if (android::base::ConsumePrefix(&line_view, kClientIdPrefix)) {
        client_id = android::base::Trim(line_view);
        continue;
      }
      static constexpr std::string_view kClientSecretPrefix = "client_secret =";
      if (android::base::ConsumePrefix(&line_view, kClientSecretPrefix)) {
        client_secret = android::base::Trim(line_view);
        continue;
      }
      static constexpr std::string_view kRefreshTokenPrefix =
          "gs_oauth2_refresh_token =";
      if (android::base::ConsumePrefix(&line_view, kRefreshTokenPrefix)) {
        refresh_token = android::base::Trim(line_view);
        continue;
      }
    }
    return std::make_unique<RefreshTokenCredentialSource>(
        http_client, CF_EXPECT(std::move(client_id)),
        CF_EXPECT(std::move(client_secret)),
        CF_EXPECT(std::move(refresh_token)));
  }
  auto json = CF_EXPECT(ParseJson(oauth_contents));
  if (json.isMember("refresh_token")) {
    return CF_EXPECT(FromJson(http_client, json));
  }
  if (json.isMember("data")) {  // acloud style
    auto& data = json["data"];
    CF_EXPECT(data.type() == Json::ValueType::arrayValue);

    CF_EXPECT(data.size() == 1);
    auto& data_first = data[0];
    CF_EXPECT(data_first.type() == Json::ValueType::objectValue);

    CF_EXPECT(data_first.isMember("credential"));
    auto& credential = data_first["credential"];
    CF_EXPECT(credential.type() == Json::ValueType::objectValue);

    return CF_EXPECT(FromJson(http_client, credential));
  } else if (json.isMember("cache")) {  // luci/chrome style
    auto& cache = json["cache"];
    CF_EXPECT_EQ(cache.type(), Json::ValueType::arrayValue);

    CF_EXPECT_EQ(cache.size(), 1);
    auto& cache_first = cache[0];
    CF_EXPECT_EQ(cache_first.type(), Json::ValueType::objectValue);

    CF_EXPECT(cache_first.isMember("token"));
    auto& token = cache_first["token"];
    CF_EXPECT_EQ(token.type(), Json::ValueType::objectValue);

    CF_EXPECT(token.isMember("refresh_token"));
    auto& refresh_token = token["refresh_token"];
    CF_EXPECT_EQ(refresh_token.type(), Json::ValueType::stringValue);

    // https://source.chromium.org/search?q=ClientSecret:%5Cs%2B%5C%22
    static constexpr char kClientId[] =
        "446450136466-mj75ourhccki9fffaq8bc1e50di315po.apps.googleusercontent."
        "com";
    static constexpr char kClientSecret[] =
        "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";

    return std::make_unique<RefreshTokenCredentialSource>(
        http_client, kClientId, kClientSecret, refresh_token.asString());
  }
  return CF_ERR("Unknown credential file format");
}

RefreshTokenCredentialSource::RefreshTokenCredentialSource(
    HttpClient& http_client, const std::string& client_id,
    const std::string& client_secret, const std::string& refresh_token)
    : http_client_(http_client),
      client_id_(client_id),
      client_secret_(client_secret),
      refresh_token_(refresh_token) {}

Result<std::pair<std::string, std::chrono::seconds>>
RefreshTokenCredentialSource::Refresh() {
  std::vector<std::string> headers = {
      "Content-Type: application/x-www-form-urlencoded"};
  std::stringstream data;
  data << "client_id=" << UrlEscape(client_id_) << "&";
  data << "client_secret=" << UrlEscape(client_secret_) << "&";
  data << "refresh_token=" << UrlEscape(refresh_token_) << "&";
  data << "grant_type=refresh_token";

  static constexpr char kUrl[] = "https://oauth2.googleapis.com/token";
  auto response =
      CF_EXPECT(HttpPostToJson(http_client_, kUrl, data.str(), headers));
  CF_EXPECT(response.HttpSuccess(), response.data);
  auto& json = response.data;

  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  CF_EXPECT(json.isMember("access_token") && json.isMember("expires_in"),
            "Refresh credential was missing access_token or expires_in."
                << " Full response was " << json << "");

  return {{
      json["access_token"].asString(),
      std::chrono::seconds(json["expires_in"].asInt()),
  }};
}

static std::string CollectSslErrors() {
  std::stringstream errors;
  auto callback = [](const char* str, size_t len, void* stream) {
    ((std::stringstream*)stream)->write(str, len);
    return 1;  // success
  };
  ERR_print_errors_cb(callback, &errors);
  return errors.str();
}

Result<std::unique_ptr<ServiceAccountOauthCredentialSource>>
ServiceAccountOauthCredentialSource::FromJson(HttpClient& http_client,
                                              const Json::Value& json,
                                              const std::string& scope) {
  std::unique_ptr<ServiceAccountOauthCredentialSource> source(
      new ServiceAccountOauthCredentialSource(http_client));
  source->scope_ = scope;

  CF_EXPECT(json.isMember("client_email"));
  CF_EXPECT(json["client_email"].type() == Json::ValueType::stringValue);
  source->email_ = json["client_email"].asString();

  CF_EXPECT(json.isMember("private_key"));
  CF_EXPECT(json["private_key"].type() == Json::ValueType::stringValue);
  std::string key_str = json["private_key"].asString();

  std::unique_ptr<BIO, int (*)(BIO*)> bo(CF_EXPECT(BIO_new(BIO_s_mem())),
                                         BIO_free);
  CF_EXPECT(BIO_write(bo.get(), key_str.c_str(), key_str.size()) ==
            key_str.size());

  auto pkey = CF_EXPECT(PEM_read_bio_PrivateKey(bo.get(), nullptr, 0, 0),
                        CollectSslErrors());
  source->private_key_.reset(pkey);

  return source;
}

ServiceAccountOauthCredentialSource::ServiceAccountOauthCredentialSource(
    HttpClient& http_client)
    : http_client_(http_client), private_key_(nullptr, EVP_PKEY_free) {}

static Result<std::string> Base64Url(const char* data, size_t size) {
  std::string base64;
  CF_EXPECT(EncodeBase64(data, size, &base64));
  base64 = android::base::StringReplace(base64, "+", "-", /* all */ true);
  base64 = android::base::StringReplace(base64, "/", "_", /* all */ true);
  return base64;
}

static Result<std::string> JsonToBase64Url(const Json::Value& json) {
  Json::StreamWriterBuilder factory;
  auto serialized = Json::writeString(factory, json);
  return CF_EXPECT(Base64Url(serialized.c_str(), serialized.size()));
}

static Result<std::string> CreateJwt(const std::string& email,
                                     const std::string& scope,
                                     EVP_PKEY* private_key) {
  using std::chrono::duration_cast;
  using std::chrono::minutes;
  using std::chrono::seconds;
  using std::chrono::system_clock;
  // https://developers.google.com/identity/protocols/oauth2/service-account
  Json::Value header_json;
  header_json["alg"] = "RS256";
  header_json["typ"] = "JWT";
  std::string header_str = CF_EXPECT(JsonToBase64Url(header_json));

  Json::Value claim_set_json;
  claim_set_json["iss"] = email;
  claim_set_json["scope"] = scope;
  claim_set_json["aud"] = "https://oauth2.googleapis.com/token";
  auto time = system_clock::now();
  claim_set_json["iat"] =
      (Json::Value::UInt64)duration_cast<seconds>(time.time_since_epoch())
          .count();
  auto exp = time + minutes(30);
  claim_set_json["exp"] =
      (Json::Value::UInt64)duration_cast<seconds>(exp.time_since_epoch())
          .count();
  std::string claim_set_str = CF_EXPECT(JsonToBase64Url(claim_set_json));

  std::string jwt_to_sign = header_str + "." + claim_set_str;

  std::unique_ptr<EVP_MD_CTX, void (*)(EVP_MD_CTX*)> sign_ctx(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  CF_EXPECT(EVP_DigestSignInit(sign_ctx.get(), nullptr, EVP_sha256(), nullptr,
                               private_key));
  CF_EXPECT(EVP_DigestSignUpdate(sign_ctx.get(), jwt_to_sign.c_str(),
                                 jwt_to_sign.size()));
  size_t length;
  CF_EXPECT(EVP_DigestSignFinal(sign_ctx.get(), nullptr, &length));
  std::vector<uint8_t> sig_raw(length);
  CF_EXPECT(EVP_DigestSignFinal(sign_ctx.get(), sig_raw.data(), &length));

  auto signature = CF_EXPECT(Base64Url((const char*)sig_raw.data(), length));
  return jwt_to_sign + "." + signature;
}

Result<std::pair<std::string, std::chrono::seconds>>
ServiceAccountOauthCredentialSource::Refresh() {
  static constexpr char URL[] = "https://oauth2.googleapis.com/token";
  static constexpr char GRANT[] = "urn:ietf:params:oauth:grant-type:jwt-bearer";
  std::stringstream content;
  content << "grant_type=" << UrlEscape(GRANT) << "&";
  auto jwt = CF_EXPECT(CreateJwt(email_, scope_, private_key_.get()));
  content << "assertion=" << UrlEscape(jwt);
  std::vector<std::string> headers = {
      "Content-Type: application/x-www-form-urlencoded"};
  auto response =
      CF_EXPECT(HttpPostToJson(http_client_, URL, content.str(), headers));
  CF_EXPECT(response.HttpSuccess(),
            "Error fetching credentials. The server response was \""
                << response.data << "\", and code was " << response.http_code);
  Json::Value json = response.data;

  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  CF_EXPECT(json.isMember("access_token") && json.isMember("expires_in"),
            "Service account credential was missing access_token or expires_in."
                << " Full response was " << json << "");

  return {{
      json["access_token"].asString(),
      std::chrono::seconds(json["expires_in"].asInt()),
  }};
}

}  // namespace

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

Result<std::unique_ptr<CredentialSource>> CreateRefreshTokenCredentialSource(
    HttpClient& http_client, const std::string& client_id,
    const std::string& client_secret, const std::string& refresh_token) {
  return std::make_unique<RefreshTokenCredentialSource>(
      http_client, client_id, client_secret, refresh_token);
}

}  // namespace cuttlefish
