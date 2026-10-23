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
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_replace.h"
#include "json/value.h"
#include "json/writer.h"
#include "openssl/base.h"
#include "openssl/bio.h"
#include "openssl/digest.h"
#include "openssl/err.h"
#include "openssl/evp.h"
#include "openssl/pem.h"

#include "cuttlefish/common/libs/utils/base64.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_json.h"
#include "cuttlefish/host/libs/web/http_client/url_escape.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

std::string CollectSslErrors() {
  std::stringstream errors;
  auto callback = [](const char* str, size_t len, void* stream) {
    ((std::stringstream*)stream)->write(str, len);
    return 1;  // success
  };
  ERR_print_errors_cb(callback, &errors);
  return errors.str();
}

Result<std::string> Base64Url(const char* data, size_t size) {
  std::string base64 = CF_EXPECT(EncodeBase64(data, size));
  absl::StrReplaceAll({{"+", "-"}, {"/", "_"}}, &base64);
  return base64;
}

Result<std::string> JsonToBase64Url(const Json::Value& json) {
  Json::StreamWriterBuilder factory;
  auto serialized = Json::writeString(factory, json);
  return CF_EXPECT(Base64Url(serialized.c_str(), serialized.size()));
}

Result<std::string> CreateJwt(const std::string& email,
                              const std::string& scope, EVP_PKEY* private_key) {
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

  std::unique_ptr<EVP_MD_CTX, void (*)(EVP_MD_CTX*)> sign_ctx(EVP_MD_CTX_new(),
                                                              EVP_MD_CTX_free);
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

}  // namespace

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

}  // namespace cuttlefish
