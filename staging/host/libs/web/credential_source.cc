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

#include "host/libs/web/credential_source.h"

#include <fcntl.h>

#include <chrono>
#include <fstream>
#include <istream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <json/json.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "common/libs/utils/base64.h"
#include "common/libs/utils/files.h"
#include "common/libs/utils/result.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {
namespace {

constexpr auto kRefreshWindow = std::chrono::minutes(2);

std::unique_ptr<CredentialSource> TryParseServiceAccount(
    HttpClient& http_client, const std::string& file_content) {
  Json::Reader reader;
  Json::Value content;
  if (!reader.parse(file_content, content)) {
    // Don't log the actual content of the file since it could be the actual
    // access token.
    LOG(DEBUG) << "Could not parse credential file as Service Account";
    return {};
  }
  auto result = ServiceAccountOauthCredentialSource::FromJson(
      http_client, content, kBuildScope);
  if (!result.ok()) {
    LOG(DEBUG) << "Failed to load service account json file: \n"
               << result.error().FormatForEnv();
    return {};
  }
  return std::unique_ptr<CredentialSource>(
      new ServiceAccountOauthCredentialSource(std::move(*result)));
}

Result<std::unique_ptr<CredentialSource>> GetCredentialSourceLegacy(
    HttpClient& http_client, const std::string& credential_source,
    const std::string& oauth_filepath) {
  std::unique_ptr<CredentialSource> result;
  if (credential_source == "gce") {
    result = GceMetadataCredentialSource::Make(http_client);
  } else if (credential_source == "") {
    LOG(VERBOSE) << "Probing acloud credentials at " << oauth_filepath;
    if (FileExists(oauth_filepath)) {
      std::ifstream stream(oauth_filepath);
      auto attempt_load =
          RefreshCredentialSource::FromOauth2ClientFile(http_client, stream);
      if (attempt_load.ok()) {
        result.reset(new RefreshCredentialSource(std::move(*attempt_load)));
      } else {
        LOG(DEBUG) << "Failed to load acloud credentials: "
                   << attempt_load.error().FormatForEnv();
      }
    } else {
      LOG(INFO) << "\"" << oauth_filepath
                << "\" missing, running without credentials";
    }
  } else if (!FileExists(credential_source)) {
    // If the parameter doesn't point to an existing file it must be the
    // credentials.
    result = FixedCredentialSource::Make(credential_source);
  } else {
    // Read the file only once in case it's a pipe.
    LOG(DEBUG) << "Attempting to open credentials file \"" << credential_source
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

}  // namespace

GceMetadataCredentialSource::GceMetadataCredentialSource(
    HttpClient& http_client)
    : http_client(http_client) {
  latest_credential = "";
  expiration = std::chrono::steady_clock::now();
}

Result<std::string> GceMetadataCredentialSource::Credential() {
  if (expiration - std::chrono::steady_clock::now() < kRefreshWindow) {
    CF_EXPECT(RefreshCredential());
  }
  return latest_credential;
}

Result<void> GceMetadataCredentialSource::RefreshCredential() {
  static constexpr char kRefreshUrl[] =
      "http://metadata.google.internal/computeMetadata/v1/instance/"
      "service-accounts/default/token";
  auto response = CF_EXPECT(
      http_client.DownloadToJson(kRefreshUrl, {"Metadata-Flavor: Google"}));
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

  expiration = std::chrono::steady_clock::now() +
               std::chrono::seconds(json["expires_in"].asInt());
  latest_credential = json["access_token"].asString();
  return {};
}

std::unique_ptr<CredentialSource> GceMetadataCredentialSource::Make(
    HttpClient& http_client) {
  return std::unique_ptr<CredentialSource>(
      new GceMetadataCredentialSource(http_client));
}

FixedCredentialSource::FixedCredentialSource(const std::string& credential) {
  this->credential = credential;
}

Result<std::string> FixedCredentialSource::Credential() { return credential; }

std::unique_ptr<CredentialSource> FixedCredentialSource::Make(
    const std::string& credential) {
  return std::unique_ptr<CredentialSource>(new FixedCredentialSource(credential));
}

Result<RefreshCredentialSource> RefreshCredentialSource::FromOauth2ClientFile(
    HttpClient& http_client, std::istream& stream) {
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value json;
  std::string errorMessage;
  CF_EXPECT(Json::parseFromStream(builder, stream, &json, &errorMessage),
            "Failed to parse json: " << errorMessage);
  CF_EXPECT(json.isMember("data"));
  auto& data = json["data"];
  CF_EXPECT(data.type() == Json::ValueType::arrayValue);

  CF_EXPECT(data.size() == 1);
  auto& data_first = data[0];
  CF_EXPECT(data_first.type() == Json::ValueType::objectValue);

  CF_EXPECT(data_first.isMember("credential"));
  auto& credential = data_first["credential"];
  CF_EXPECT(credential.type() == Json::ValueType::objectValue);

  CF_EXPECT(credential.isMember("client_id"));
  auto& client_id = credential["client_id"];
  CF_EXPECT(client_id.type() == Json::ValueType::stringValue);

  CF_EXPECT(credential.isMember("client_secret"));
  auto& client_secret = credential["client_secret"];
  CF_EXPECT(client_secret.type() == Json::ValueType::stringValue);

  CF_EXPECT(credential.isMember("refresh_token"));
  auto& refresh_token = credential["refresh_token"];
  CF_EXPECT(refresh_token.type() == Json::ValueType::stringValue);

  return RefreshCredentialSource(http_client, client_id.asString(),
                                 client_secret.asString(),
                                 refresh_token.asString());
}

RefreshCredentialSource::RefreshCredentialSource(
    HttpClient& http_client, const std::string& client_id,
    const std::string& client_secret, const std::string& refresh_token)
    : http_client_(http_client),
      client_id_(client_id),
      client_secret_(client_secret),
      refresh_token_(refresh_token) {}

Result<std::string> RefreshCredentialSource::Credential() {
  if (expiration_ - std::chrono::steady_clock::now() < kRefreshWindow) {
    CF_EXPECT(UpdateLatestCredential());
  }
  return latest_credential_;
}

Result<void> RefreshCredentialSource::UpdateLatestCredential() {
  std::vector<std::string> headers = {
      "Content-Type: application/x-www-form-urlencoded"};
  std::stringstream data;
  data << "client_id=" << http_client_.UrlEscape(client_id_) << "&";
  data << "client_secret=" << http_client_.UrlEscape(client_secret_) << "&";
  data << "refresh_token=" << http_client_.UrlEscape(refresh_token_) << "&";
  data << "grant_type=refresh_token";

  static constexpr char kUrl[] = "https://oauth2.googleapis.com/token";
  auto response = CF_EXPECT(http_client_.PostToJson(kUrl, data.str(), headers));
  CF_EXPECT(response.HttpSuccess(), response.data);
  auto& json = response.data;

  CF_EXPECT(!json.isMember("error"),
            "Response had \"error\" but had http success status. Received \""
                << json << "\"");

  CF_EXPECT(json.isMember("access_token") && json.isMember("expires_in"),
            "Refresh credential was missing access_token or expires_in."
                << " Full response was " << json << "");

  expiration_ = std::chrono::steady_clock::now() +
                std::chrono::seconds(json["expires_in"].asInt());
  latest_credential_ = json["access_token"].asString();
  return {};
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

Result<ServiceAccountOauthCredentialSource>
ServiceAccountOauthCredentialSource::FromJson(HttpClient& http_client,
                                              const Json::Value& json,
                                              const std::string& scope) {
  ServiceAccountOauthCredentialSource source(http_client);
  source.scope_ = scope;

  CF_EXPECT(json.isMember("client_email"));
  CF_EXPECT(json["client_email"].type() == Json::ValueType::stringValue);
  source.email_ = json["client_email"].asString();

  CF_EXPECT(json.isMember("private_key"));
  CF_EXPECT(json["private_key"].type() == Json::ValueType::stringValue);
  std::string key_str = json["private_key"].asString();

  std::unique_ptr<BIO, int (*)(BIO*)> bo(CF_EXPECT(BIO_new(BIO_s_mem())),
                                         BIO_free);
  CF_EXPECT(BIO_write(bo.get(), key_str.c_str(), key_str.size()) ==
            key_str.size());

  auto pkey = CF_EXPECT(PEM_read_bio_PrivateKey(bo.get(), nullptr, 0, 0),
                        CollectSslErrors());
  source.private_key_.reset(pkey);

  return source;
}

ServiceAccountOauthCredentialSource::ServiceAccountOauthCredentialSource(
    HttpClient& http_client)
    : http_client_(http_client), private_key_(nullptr, EVP_PKEY_free) {}

static Result<std::string> Base64Url(const char* data, std::size_t size) {
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
      (uint64_t)duration_cast<seconds>(time.time_since_epoch()).count();
  auto exp = time + minutes(30);
  claim_set_json["exp"] =
      (uint64_t)duration_cast<seconds>(exp.time_since_epoch()).count();
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

Result<void> ServiceAccountOauthCredentialSource::RefreshCredential() {
  static constexpr char URL[] = "https://oauth2.googleapis.com/token";
  static constexpr char GRANT[] = "urn:ietf:params:oauth:grant-type:jwt-bearer";
  std::stringstream content;
  content << "grant_type=" << http_client_.UrlEscape(GRANT) << "&";
  auto jwt = CF_EXPECT(CreateJwt(email_, scope_, private_key_.get()));
  content << "assertion=" << http_client_.UrlEscape(jwt);
  std::vector<std::string> headers = {
      "Content-Type: application/x-www-form-urlencoded"};
  auto response =
      CF_EXPECT(http_client_.PostToJson(URL, content.str(), headers));
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

  expiration_ = std::chrono::steady_clock::now() +
                std::chrono::seconds(json["expires_in"].asInt());
  latest_credential_ = json["access_token"].asString();
  return {};
}

Result<std::string> ServiceAccountOauthCredentialSource::Credential() {
  if (expiration_ - std::chrono::steady_clock::now() < kRefreshWindow) {
    CF_EXPECT(RefreshCredential());
  }
  return latest_credential_;
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
  // when this helper is removed its `.acloud2_oauth2.dat` processing should be
  // moved here
  return GetCredentialSourceLegacy(http_client, credential_source,
                                   oauth_filepath);
}

}  // namespace cuttlefish
