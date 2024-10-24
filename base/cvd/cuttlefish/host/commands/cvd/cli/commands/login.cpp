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
#include "host/commands/cvd/cli/commands/login.h"

#include <unistd.h>

#include <memory>
#include <string>

#include <zlib.h>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/environment.h"
#include "common/libs/utils/flag_parser.h"
#include "common/libs/utils/subprocess.h"
#include "host/commands/cvd/cli/commands/server_handler.h"
#include "host/commands/cvd/cli/utils.h"
#include "host/libs/directories/xdg.h"
#include "host/libs/web/http_client/curl_global_init.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {
namespace {

using android::base::Tokenize;

constexpr char kSummaryHelpText[] = "Acquire credentials";

constexpr char kHelpMessage[] = R"(
usage: cvd login --client_id=CLIENT_ID --client_secret=SECRET --scopes=SCOPES [--ssh]

  `cvd login` will request a credential to the Android Build API and store it in
  persistent local storage.
)";

struct LoginFlags {
  std::vector<Flag> Flags() {
    return {
        GflagsCompatFlag("client_id", client_id),
        GflagsCompatFlag("client_secret", client_secret),
        GflagsCompatFlag("scopes", scopes),
        GflagsCompatFlag("ssh", ssh),
    };
  }

  std::string client_id;
  std::string client_secret;
  std::vector<std::string> scopes;
  // Imperfect detection: the user may ssh into an existing `screen` or `tmux`
  // session.
  bool ssh = StringFromEnv("SSH_CLIENT").has_value() ||
             StringFromEnv("SSH_TTY").has_value();
};

Result<std::string> CodeFromUrl(std::string_view url) {
  static constexpr std::string_view kCodeEq = "code=";
  std::size_t code_begin = url.find(kCodeEq);
  CF_EXPECTF(code_begin != std::string_view::npos, "No '{}'", kCodeEq);

  std::size_t code_end = url.find("&", code_begin + 2);
  if (code_end != std::string::npos) {
    code_end = code_end - code_begin - kCodeEq.size();
  }
  // npos is an acceptable end value for substr
  return std::string(url.substr(code_begin + kCodeEq.size(), code_end));
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

    std::string code = CF_EXPECT(CodeFromUrl(request_lines[0]));

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

struct Credentials {
  static constexpr char kAccessToken[] = "access_token";
  static constexpr char kExpiresIn[] = "expires_in";
  static constexpr char kIdToken[] = "id_token";
  static constexpr char kRefreshToken[] = "refresh_token";
  static constexpr char kScope[] = "scope";
  static constexpr char kTokenType[] = "token_type";

  static Result<Credentials> Request(const LoginFlags& flags) {
    CurlGlobalInit init;

    std::unique_ptr<HttpServer> http_server =
        std::make_unique<HttpServer>(CF_EXPECT(HttpServer::Create()));
    uint16_t port = http_server->Port();

    std::unique_ptr<HttpClient> http_client =
        HttpClient::CurlClient(NameResolver(), true);
    CF_EXPECT(http_client.get(), "Failed to create a http client");

    std::string redirect_uri = fmt::format("http://localhost:{}", port);
    std::string scopes_str = android::base::Join(flags.scopes, " ");

    // https://developers.google.com/identity/protocols/oauth2/native-app
    std::stringstream consent;
    consent << "https://accounts.google.com/o/oauth2/v2/auth?";
    consent << "client_id=" << http_client->UrlEscape(flags.client_id) << "&";
    consent << "redirect_uri=" << http_client->UrlEscape(redirect_uri) << "&";
    consent << "response_type=code&";
    consent << "scope=" << http_client->UrlEscape(scopes_str) << "&";

    std::string code;

    if (flags.ssh) {
      http_server.reset(nullptr);

      std::cout << "Open this URL in your browser: " << consent.rdbuf();

      std::cout << "\n\nThis leads to a 'connection refused' page.\n";
      std::cout << "Copy and paste that page's URL here: ";

      std::string code_url;
      std::getline(std::cin, code_url);

      code = CF_EXPECT(CodeFromUrl(code_url));
    } else {
      std::cout << "Opening a browser for the consent flow.\n";
      std::cout << "Using SSH? Please run this command again with `--ssh`.\n";

      CF_EXPECT_EQ(Execute({"/usr/bin/xdg-open", consent.str()}), 0);

      code = CF_EXPECT(http_server->CodeFromClient());
    }

    // TODO: schuffelen - Deduplicate with `RefreshCredentialSource::Refresh()`
    std::stringstream exchange;
    exchange << "code=" << code << "&";
    exchange << "client_id=" << flags.client_id << "&";
    exchange << "client_secret=" << flags.client_secret << "&";
    exchange << "redirect_uri=" << redirect_uri << "&";
    exchange << "grant_type=authorization_code";

    constexpr char kExchangeUrl[] = "https://oauth2.googleapis.com/token";
    std::vector<std::string> headers = {
        "Content-Type: application/x-www-form-urlencoded"};
    Json::Value token_json =
        CF_EXPECT(
            http_client->PostToJson(kExchangeUrl, exchange.str(), headers))
            .data;

    CF_EXPECT(!token_json.isMember("error"),
              "Response had \"error\" but had http success status. Received '"
                  << token_json << "'");

    return CF_EXPECT(FromJson(token_json));
  }

  static Result<Credentials> FromJson(const Json::Value& token_json) {
    // TODO: schuffelen - Deduplicate with
    // `RefreshCredentialSource::FromOauth2ClientFile`
    Credentials credentials;

    CF_EXPECTF(token_json.isMember(kAccessToken), "No '{}'", kAccessToken);
    CF_EXPECT_EQ(token_json[kAccessToken].type(), Json::ValueType::stringValue);
    credentials.access_token = token_json[kAccessToken].asString();

    CF_EXPECTF(token_json.isMember(kExpiresIn), "No '{}'", kExpiresIn);
    CF_EXPECT_EQ(token_json[kExpiresIn].type(), Json::ValueType::intValue);
    std::size_t seconds_num = token_json[kExpiresIn].asUInt();
    std::chrono::seconds seconds(seconds_num);
    credentials.expires = std::chrono::steady_clock::now() + seconds;

    if (token_json.isMember(kIdToken)) {
      CF_EXPECT_EQ(token_json[kIdToken].type(), Json::ValueType::stringValue);
      credentials.id_token = token_json[kIdToken].asString();
    }

    CF_EXPECTF(token_json.isMember(kRefreshToken), "No '{}'", kRefreshToken);
    CF_EXPECT_EQ(token_json[kRefreshToken].type(),
                 Json::ValueType::stringValue);
    credentials.refresh_token = token_json[kRefreshToken].asString();

    CF_EXPECTF(token_json.isMember(kScope), "No '{}'", kScope);
    if (token_json[kScope].type() == Json::ValueType::stringValue) {
      credentials.scope = Tokenize(token_json[kScope].asString(), " ");
    } else if (token_json[kScope].type() == Json::ValueType::arrayValue) {
      for (const Json::Value& scope : token_json[kScope]) {
        CF_EXPECT_EQ(scope.type(), Json::ValueType::stringValue);
        credentials.scope.emplace_back(scope.asString());
      }
    } else {
      return CF_ERRF("Unexpected type for {}", kScope);
    }

    CF_EXPECTF(token_json.isMember(kTokenType), "No '{}'", kTokenType);
    CF_EXPECT_EQ(token_json[kTokenType].type(), Json::ValueType::stringValue);
    credentials.token_type = token_json[kTokenType].asString();

    std::cout << "Success\n";

    return credentials;
  }

  Json::Value ToJson() const {
    Json::Value json;
    json[kAccessToken] = access_token;
    json[kExpiresIn] = 0;  // `expires_in` is not meaningful in storage
    if (id_token.has_value()) {
      json[kIdToken] = *id_token;
    }
    json[kRefreshToken] = refresh_token;
    json[kScope] = android::base::Join(scope, " ");
    json[kTokenType] = token_type;
    return json;
  }

  std::string ShortName() const {
    std::string scopes_str = android::base::Join(scope, " ");

    unsigned char* data = (unsigned char*)scopes_str.data();
    return std::to_string(crc32(0, data, scopes_str.size()));
  }

  std::string access_token;
  std::chrono::steady_clock::time_point expires;
  std::optional<std::string> id_token;
  std::string refresh_token;
  std::vector<std::string> scope;
  std::string token_type;
};

class CvdLoginCommand : public CvdServerHandler {
 public:
  Result<bool> CanHandle(const CommandRequest& request) const override {
    CommandInvocation invocation = ParseInvocation(request);
    return invocation.command == "login";
  }

  Result<cvd::Response> Handle(const CommandRequest& request) override {
    CF_EXPECT(CanHandle(request));

    std::vector<std::string> args = request.Args();
    LoginFlags flags = {};
    CF_EXPECT(ConsumeFlags(flags.Flags(), args), "Failed to parse arguments");

    Credentials credentials = CF_EXPECT(Credentials::Request(flags));

    // TODO: schuffelen - Deduplicate with RefreshCredentialSource
    Json::Value file_json;
    file_json["data"] = credentials.ToJson();

    std::string file_name =
        fmt::format("credentials/{}.json", credentials.ShortName());
    CF_EXPECT(WriteCvdDataFile(file_name, file_json.toStyledString()));

    return {};
  }

  cvd_common::Args CmdList() const override { return {"login"}; }

  Result<std::string> SummaryHelp() const override { return kSummaryHelpText; }

  bool ShouldInterceptHelp() const override { return true; }

  Result<std::string> DetailedHelp(std::vector<std::string>&) const override {
    return kHelpMessage;
  }
};

}  // namespace

/** Create a credentials file */
std::unique_ptr<CvdServerHandler> NewLoginCommand() {
  return std::make_unique<CvdLoginCommand>();
}

}  // namespace cuttlefish
