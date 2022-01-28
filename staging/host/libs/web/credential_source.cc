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

#include "credential_source.h"

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <json/json.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "common/libs/utils/base64.h"

namespace cuttlefish {
namespace {

std::chrono::steady_clock::duration REFRESH_WINDOW =
    std::chrono::minutes(2);
std::string REFRESH_URL = "http://metadata.google.internal/computeMetadata/"
    "v1/instance/service-accounts/default/token";

} // namespace

GceMetadataCredentialSource::GceMetadataCredentialSource(CurlWrapper& curl)
    : curl(curl) {
  latest_credential = "";
  expiration = std::chrono::steady_clock::now();
}

std::string GceMetadataCredentialSource::Credential() {
  if (expiration - std::chrono::steady_clock::now() < REFRESH_WINDOW) {
    RefreshCredential();
  }
  return latest_credential;
}

void GceMetadataCredentialSource::RefreshCredential() {
  auto curl_response =
      curl.DownloadToJson(REFRESH_URL, {"Metadata-Flavor: Google"});
  const auto& json = curl_response.data;
  if (!curl_response.HttpSuccess()) {
    LOG(FATAL) << "Error fetching credentials. The server response was \""
               << json << "\", and code was " << curl_response.http_code;
  }
  CHECK(!json.isMember("error"))
      << "Response had \"error\" but had http success status. Received \""
      << json << "\"";

  bool has_access_token = json.isMember("access_token");
  bool has_expires_in = json.isMember("expires_in");
  if (!has_access_token || !has_expires_in) {
    LOG(FATAL) << "GCE credential was missing access_token or expires_in. "
               << "Full response was " << json << "";
  }

  expiration = std::chrono::steady_clock::now() +
               std::chrono::seconds(json["expires_in"].asInt());
  latest_credential = json["access_token"].asString();
}

std::unique_ptr<CredentialSource> GceMetadataCredentialSource::make(
    CurlWrapper& curl) {
  return std::unique_ptr<CredentialSource>(
      new GceMetadataCredentialSource(curl));
}

FixedCredentialSource::FixedCredentialSource(const std::string& credential) {
  this->credential = credential;
}

std::string FixedCredentialSource::Credential() {
  return credential;
}

std::unique_ptr<CredentialSource> FixedCredentialSource::make(
    const std::string& credential) {
  return std::unique_ptr<CredentialSource>(new FixedCredentialSource(credential));
}

static int OpenSslPrintErrCallback(const char* str, size_t len, void*) {
  LOG(ERROR) << std::string(str, len);
  return 1;  // success
}

std::unique_ptr<ServiceAccountOauthCredentialSource>
ServiceAccountOauthCredentialSource::FromJson(CurlWrapper& curl,
                                              const Json::Value& json,
                                              const std::string& scope) {
  std::unique_ptr<ServiceAccountOauthCredentialSource> source;
  source.reset(new ServiceAccountOauthCredentialSource(curl));

  if (!json.isMember("client_email")) {
    LOG(ERROR) << "Service account json missing `client_email` field";
    return {};
  } else if (json["client_email"].type() != Json::ValueType::stringValue) {
    LOG(ERROR) << "Service account `client_email` field was the wrong type";
    return {};
  }
  source->email_ = json["client_email"].asString();

  if (!json.isMember("private_key")) {
    LOG(ERROR) << "Service account json missing `private_key` field";
    return {};
  } else if (json["private_key"].type() != Json::ValueType::stringValue) {
    LOG(ERROR) << "Service account json `private_key` field was the wrong type";
    return {};
  }
  std::string key_str = json["private_key"].asString();

  std::unique_ptr<BIO, int (*)(BIO*)> bo(BIO_new(BIO_s_mem()), BIO_free);
  if (!bo) {
    LOG(ERROR) << "Failed to create boringssl BIO";
    return {};
  }
  CHECK(BIO_write(bo.get(), key_str.c_str(), key_str.size()) == key_str.size());

  source->private_key_.reset(PEM_read_bio_PrivateKey(bo.get(), nullptr, 0, 0));
  if (!source->private_key_) {
    LOG(ERROR) << "PEM_read_bio_PrivateKey failed, showing openssl error stack";
    ERR_print_errors_cb(OpenSslPrintErrCallback, nullptr);
    return {};
  }

  source->scope_ = scope;

  return source;
}

ServiceAccountOauthCredentialSource::ServiceAccountOauthCredentialSource(
    CurlWrapper& curl)
    : curl_(curl), private_key_(nullptr, EVP_PKEY_free) {}

static std::string Base64Url(const char* data, std::size_t size) {
  std::string base64;
  CHECK(EncodeBase64(data, size, &base64));
  base64 = android::base::StringReplace(base64, "+", "-", /* all */ true);
  base64 = android::base::StringReplace(base64, "/", "_", /* all */ true);
  return base64;
}

static std::string JsonToBase64Url(const Json::Value& json) {
  Json::StreamWriterBuilder factory;
  auto serialized = Json::writeString(factory, json);
  return Base64Url(serialized.c_str(), serialized.size());
}

static std::string CreateJwt(const std::string& email, const std::string& scope,
                             EVP_PKEY* private_key) {
  using std::chrono::duration_cast;
  using std::chrono::minutes;
  using std::chrono::seconds;
  using std::chrono::system_clock;
  // https://developers.google.com/identity/protocols/oauth2/service-account
  Json::Value header_json;
  header_json["alg"] = "RS256";
  header_json["typ"] = "JWT";
  std::string header_str = JsonToBase64Url(header_json);

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
  std::string claim_set_str = JsonToBase64Url(claim_set_json);

  std::string jwt_to_sign = header_str + "." + claim_set_str;

  std::unique_ptr<EVP_MD_CTX, void (*)(EVP_MD_CTX*)> sign_ctx(
      EVP_MD_CTX_create(), EVP_MD_CTX_free);
  CHECK(EVP_DigestSignInit(sign_ctx.get(), nullptr, EVP_sha256(), nullptr,
                           private_key));
  CHECK(EVP_DigestSignUpdate(sign_ctx.get(), jwt_to_sign.c_str(),
                             jwt_to_sign.size()));
  size_t length;
  CHECK(EVP_DigestSignFinal(sign_ctx.get(), nullptr, &length));
  std::vector<uint8_t> sig_raw(length);
  CHECK(EVP_DigestSignFinal(sign_ctx.get(), sig_raw.data(), &length));

  return jwt_to_sign + "." + Base64Url((const char*)sig_raw.data(), length);
}

void ServiceAccountOauthCredentialSource::RefreshCredential() {
  static constexpr char URL[] = "https://oauth2.googleapis.com/token";
  static constexpr char GRANT[] = "urn:ietf:params:oauth:grant-type:jwt-bearer";
  std::stringstream content;
  content << "grant_type=" << curl_.UrlEscape(GRANT) << "&";
  auto jwt = CreateJwt(email_, scope_, private_key_.get());
  content << "assertion=" << curl_.UrlEscape(jwt);
  std::vector<std::string> headers = {
      "Content-Type: application/x-www-form-urlencoded"};
  auto curl_response = curl_.PostToJson(URL, content.str(), headers);
  if (!curl_response.HttpSuccess()) {
    LOG(FATAL) << "Error fetching credentials. The server response was \""
               << curl_response.data << "\", and code was "
               << curl_response.http_code;
  }
  Json::Value json = curl_response.data;

  CHECK(!json.isMember("error"))
      << "Response had \"error\" but had http success status. Received \""
      << json << "\"";

  bool has_access_token = json.isMember("access_token");
  bool has_expires_in = json.isMember("expires_in");
  if (!has_access_token || !has_expires_in) {
    LOG(FATAL) << "GCE credential was missing access_token or expires_in. "
               << "Full response was " << json << "";
  }

  expiration_ = std::chrono::steady_clock::now() +
                std::chrono::seconds(json["expires_in"].asInt());
  latest_credential_ = json["access_token"].asString();
}

std::string ServiceAccountOauthCredentialSource::Credential() {
  if (expiration_ - std::chrono::steady_clock::now() < REFRESH_WINDOW) {
    RefreshCredential();
  }
  return latest_credential_;
}

} // namespace cuttlefish
