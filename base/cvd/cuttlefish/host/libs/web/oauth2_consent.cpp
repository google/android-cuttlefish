/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "host/libs/web/oauth2_consent.h"

#include <android-base/file.h>
#include <unistd.h>

#include <zlib.h>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/contains.h"
#include "common/libs/utils/json.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/web/http_client/http_client.h"
#include "host/libs/directories/xdg.h"

namespace cuttlefish {
namespace {

using android::base::Join;
using android::base::Tokenize;

Result<std::string> AuthorizationCodeFromUrl(const std::string_view url) {
  std::string_view code = url;

  static constexpr std::string_view kCodeEq = "code=";
  std::size_t code_eq_pos = code.find(kCodeEq);
  CF_EXPECTF(code_eq_pos != std::string_view::npos, "No '{}'", kCodeEq);
  code.remove_prefix(code_eq_pos + kCodeEq.size());

  std::size_t code_end_pos = code.find("&");
  if (code_end_pos != std::string::npos) {
    code = code.substr(0, code_end_pos);
  }

  return std::string(code);
}

class HttpServer {
 public:
  static Result<HttpServer> Create() {
    HttpServer server;
    // TODO: schuffelen - let the kernel choose the port
    server.server_ = SharedFD::SocketLocalServer(server.Port(), SOCK_STREAM);
    CF_EXPECT(server.server_->IsOpen(), server.server_->StrError());

    return server;
  }

  uint16_t Port() { return 8888; }

  Result<std::string> CodeFromClient() {
    SharedFD client = SharedFD::Accept(*server_);
    CF_EXPECT(client->IsOpen(), client->StrError());

    std::stringstream request;
    char buffer[512];
    ssize_t bytes_read;
    while ((bytes_read = client->Read(buffer, sizeof(buffer))) > 0) {
      request.write(buffer, bytes_read);
      std::string_view buffer_view(buffer, bytes_read);
      if (buffer_view.find("\r\n\r\n") != std::string_view::npos) {
        break;
      }
    }
    CF_EXPECT_EQ(client->GetErrno(), 0, client->StrError());

    CF_EXPECT(request.str().find("\r\n") != std::string::npos);
    std::vector<std::string> request_lines = Tokenize(request.str(), "\r\n");
    CF_EXPECT(!request_lines.empty(), "no lines in input");

    std::string code = CF_EXPECT(AuthorizationCodeFromUrl(request_lines[0]));

    static constexpr std::string_view kResponse = "Please return to the CLI.";

    std::string response = fmt::format(
        "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: text/plain; "
        "charset=utf-8\r\n\r\n{}",
        kResponse.size(), kResponse);
    CF_EXPECT_EQ(WriteAll(client, response), (ssize_t)response.size(),
                 client->StrError());

    return code;
  }

 private:
  HttpServer() = default;
  SharedFD server_;
};

uint32_t ScopeChecksum(const std::vector<std::string>& scopes) {
  std::string scopes_str = Join(scopes, " ");

  unsigned char* data = (unsigned char*)scopes_str.data();
  return crc32(0, data, scopes_str.size());
}

static constexpr char kRefreshToken[] = "refresh_token";
static constexpr char kScope[] = "scope";

Result<std::string> GetRefreshToken(HttpClient& http_client,
                                    const Oauth2ConsentRequest& request,
                                    bool ssh) {
  std::unique_ptr<HttpServer> http_server;
  uint16_t port;
  if (ssh) {
    port = 1024 + (rand() % ((1 << 16) - 1024));
  } else {
    http_server = std::make_unique<HttpServer>(CF_EXPECT(HttpServer::Create()));
    port = http_server->Port();
  }

  std::string redirect_uri = fmt::format("http://localhost:{}", port);
  std::string scopes_str = Join(request.scopes, " ");

  // https://developers.google.com/identity/protocols/oauth2/native-app
  std::stringstream consent;
  consent << "https://accounts.google.com/o/oauth2/v2/auth?";
  consent << "client_id=" << http_client.UrlEscape(request.client_id) << "&";
  consent << "redirect_uri=" << http_client.UrlEscape(redirect_uri) << "&";
  consent << "response_type=code&";
  consent << "scope=" << http_client.UrlEscape(scopes_str) << "&";

  std::string code;

  if (ssh) {
    http_server.reset(nullptr);

    std::cout << "Open this URL in your browser: " << consent.rdbuf();

    std::cout << "\n\nThis leads to a 'connection refused' page.\n";
    std::cout << "Copy and paste that page's URL here: ";

    std::string code_url;
    std::getline(std::cin, code_url);

    code = CF_EXPECT(AuthorizationCodeFromUrl(code_url));
  } else {
    std::cout << "Opening a browser for the consent flow.\n";

    CF_EXPECT_EQ(Execute({"/usr/bin/xdg-open", consent.str()}), 0);

    code = CF_EXPECT(http_server->CodeFromClient());
  }

  // TODO: schuffelen - Deduplicate with `RefreshCredentialSource::Refresh()`
  std::stringstream exchange;
  exchange << "code=" << code << "&";
  exchange << "client_id=" << request.client_id << "&";
  exchange << "client_secret=" << request.client_secret << "&";
  exchange << "redirect_uri=" << redirect_uri << "&";
  exchange << "grant_type=authorization_code";

  constexpr char kExchangeUrl[] = "https://oauth2.googleapis.com/token";
  std::vector<std::string> headers = {
      "Content-Type: application/x-www-form-urlencoded"};
  Json::Value token_json =
      CF_EXPECT(http_client.PostToJson(kExchangeUrl, exchange.str(), headers))
          .data;

  CF_EXPECT(!token_json.isMember("error"),
            "Response had \"error\" but had http success status. Received '"
                << token_json << "'");

  CF_EXPECTF(token_json.isMember(kScope), "No '{}'", kScope);
  CF_EXPECT_EQ(token_json[kScope].type(), Json::ValueType::stringValue);
  std::string response_scope = token_json[kScope].asString();
  std::vector<std::string> response_scopes = Tokenize(response_scope, " ");
  for (const std::string& scope : request.scopes) {
    CF_EXPECTF(Contains(response_scopes, scope), "Response missing '{}'",
               scope);
  }

  CF_EXPECTF(token_json.isMember(kRefreshToken), "No '{}'", kRefreshToken);
  CF_EXPECT_EQ(token_json[kRefreshToken].type(), Json::ValueType::stringValue);

  return token_json[kRefreshToken].asString();
}

static constexpr char kClientId[] = "client_id";
static constexpr char kClientSecret[] = "client_secret";
static constexpr char kCredentials[] = "credentials";

Result<std::unique_ptr<CredentialSource>> Oauth2Login(
    HttpClient& http_client, const Oauth2ConsentRequest& request, bool ssh) {
  std::string refresh_token =
      CF_EXPECT(GetRefreshToken(http_client, request, ssh));

  Json::Value serialized;
  serialized[kClientId] = request.client_id;
  serialized[kClientSecret] = request.client_secret;
  serialized[kRefreshToken] = refresh_token;
  for (const std::string& scope : request.scopes) {
    serialized[kScope].append(scope);
  }

  uint32_t checksum = ScopeChecksum(request.scopes);
  std::string filename = fmt::format("{}/{}.json", kCredentials, checksum);
  std::string contents = serialized.toStyledString();

  CF_EXPECT(WriteCvdDataFile(filename, std::move(contents)));

  return CreateRefreshTokenCredentialSource(
      http_client, request.client_id, request.client_secret, refresh_token);
}

Result<std::unique_ptr<CredentialSource>> CredentialForScopes(
    HttpClient& http_client, const std::vector<std::string>& scopes,
    const std::string& file_path) {
  std::string contents;
  CF_EXPECTF(android::base::ReadFileToString(file_path, &contents),
             "Failed to read '{}'", file_path);

  Json::Value json = CF_EXPECT(ParseJson(contents));

  CF_EXPECTF(json.isMember(kScope), "No '{}'", kScope);
  CF_EXPECT_EQ(json[kScope].type(), Json::ValueType::arrayValue);
  std::vector<std::string> file_scopes;
  for (const Json::Value& file_scope : json[kScope]) {
    CF_EXPECT_EQ(file_scope.type(), Json::ValueType::stringValue);
    file_scopes.emplace_back(file_scope.asString());
  }
  for (const std::string& scope : scopes) {
    CF_EXPECT(Contains(file_scopes, scope));
  }

  CF_EXPECTF(json.isMember(kClientId), "No '{}'", kClientId);
  CF_EXPECT_EQ(json[kClientId].type(), Json::ValueType::stringValue);
  std::string client_id = json[kClientId].asString();

  CF_EXPECTF(json.isMember(kClientSecret), "No '{}'", kClientSecret);
  CF_EXPECT_EQ(json[kClientSecret].type(), Json::ValueType::stringValue);
  std::string client_secret = json[kClientSecret].asString();

  CF_EXPECTF(json.isMember(kRefreshToken), "No '{}'", kRefreshToken);
  CF_EXPECT_EQ(json[kRefreshToken].type(), Json::ValueType::stringValue);
  std::string refresh_token = json[kRefreshToken].asString();

  return CreateRefreshTokenCredentialSource(http_client, client_id,
                                            client_secret, refresh_token);
}

}  // namespace

Result<std::unique_ptr<CredentialSource>> Oauth2LoginLocal(
    HttpClient& http_client, const Oauth2ConsentRequest& request) {
  return CF_EXPECT(Oauth2Login(http_client, request, false));
}

Result<std::unique_ptr<CredentialSource>> Oauth2LoginSsh(
    HttpClient& http_client, const Oauth2ConsentRequest& request) {
  return CF_EXPECT(Oauth2Login(http_client, request, true));
}

Result<std::unique_ptr<CredentialSource>> CredentialForScopes(
    HttpClient& http_client, const std::vector<std::string>& scopes) {
  std::vector<std::string> credential_paths =
      CF_EXPECT(FindCvdDataFiles(kCredentials));

  for (const std::string& credential_path : credential_paths) {
    if (!android::base::EndsWith(credential_path, ".json")) {
      continue;
    }
    Result<std::unique_ptr<CredentialSource>> credential =
        CredentialForScopes(http_client, scopes, credential_path);
    if (credential.ok() && credential->get() != nullptr) {
      return std::move(*credential);
    }
  }
  return {};
}

}  // namespace cuttlefish
