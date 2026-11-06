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

#include "cuttlefish/host/libs/web/credential_source/refresh_token_credential_source.h"

#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "json/value.h"
#include "json/writer.h"

#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/host/libs/web/credential_source/credential_source.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_json.h"
#include "cuttlefish/host/libs/web/http_client/url_escape.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

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

  return std::unique_ptr<RefreshTokenCredentialSource>(
      new RefreshTokenCredentialSource(http_client, client_id.asString(),
                                       client_secret.asString(),
                                       refresh_token.asString()));
}

Result<std::unique_ptr<RefreshTokenCredentialSource>>
RefreshTokenCredentialSource::FromOauth2ClientFile(
    HttpClient& http_client, const std::string& oauth_contents) {
  if (absl::StartsWith(oauth_contents, "[OAuth2]")) {  // .boto file
    std::string client_id = "";
    std::string client_secret = "";
    std::string refresh_token = "";
    for (std::string_view line :
         absl::StrSplit(oauth_contents, '\n', absl::SkipEmpty())) {
      static constexpr std::string_view kClientIdPrefix = "client_id =";
      if (absl::ConsumePrefix(&line, kClientIdPrefix)) {
        client_id = std::string(absl::StripAsciiWhitespace(line));
        continue;
      }
      static constexpr std::string_view kClientSecretPrefix = "client_secret =";
      if (absl::ConsumePrefix(&line, kClientSecretPrefix)) {
        client_secret = std::string(absl::StripAsciiWhitespace(line));
        continue;
      }
      static constexpr std::string_view kRefreshTokenPrefix =
          "gs_oauth2_refresh_token =";
      if (absl::ConsumePrefix(&line, kRefreshTokenPrefix)) {
        refresh_token = std::string(absl::StripAsciiWhitespace(line));
        continue;
      }
    }
    CF_EXPECT(!client_id.empty());
    CF_EXPECT(!client_secret.empty());
    CF_EXPECT(!refresh_token.empty());
    return std::unique_ptr<RefreshTokenCredentialSource>(
        new RefreshTokenCredentialSource(http_client, client_id, client_secret,
                                         refresh_token));
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

    return std::unique_ptr<RefreshTokenCredentialSource>(
        new RefreshTokenCredentialSource(http_client, kClientId, kClientSecret,
                                         refresh_token.asString()));
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

Result<std::unique_ptr<CredentialSource>> RefreshTokenCredentialSource::Make(
    HttpClient& http_client, const std::string& client_id,
    const std::string& client_secret, const std::string& refresh_token) {
  return std::unique_ptr<CredentialSource>(new RefreshTokenCredentialSource(
      http_client, client_id, client_secret, refresh_token));
}

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

}  // namespace cuttlefish
