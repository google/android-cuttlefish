//
// Copyright (C) 2022 The Android Open Source Project
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

#include "host/libs/web/http_client/sso_client.h"

#include <iostream>
#include <regex>

#include "common/libs/utils/subprocess.h"

namespace cuttlefish {
namespace http_client {
namespace {

constexpr char kSsoClientBin[] = "/usr/bin/sso_client";

// Matches the sso_client's standard output when it succeeds expecting a valid
// http response.
const std::regex kStdoutRegex(
    "HTTP/\\d+\\.\\d+\\s(\\d+)\\s.+\r\n" /* status */
    "(?:.+\r\n)+\r\n"                    /* headers */
    "(.+)?"                              /* body */
    "\n?" /* new line added by the sso_client if a body exists */);

enum class HttpMethod {
  kGet,
  kPost,
  kDelete,
};

const char* kHttpMethodStrings[] = {"GET", "POST", "DELETE"};

Result<HttpResponse<std::string>> MakeRequest(
    ExecCmdFunc exec_cmd_func_, const std::string& url,
    HttpMethod method = HttpMethod::kGet, const std::string& data = "") {
  Command sso_client_cmd(kSsoClientBin);
  sso_client_cmd.AddParameter("--use_master_cookie");
  sso_client_cmd.AddParameter("--request_timeout=300");  // 5 minutes
  sso_client_cmd.AddParameter("--dump_header");
  sso_client_cmd.AddParameter("--url=" + url);
  sso_client_cmd.AddParameter("--method=" +
                              std::string(kHttpMethodStrings[(int)method]));
  if (method == HttpMethod::kPost) {
    if (!data.empty()) {
      sso_client_cmd.AddParameter("--data=" + data);
    }
  }
  std::string stdout_, stderr_;
  int ret = exec_cmd_func_(std::move(sso_client_cmd), nullptr, &stdout_,
                           &stderr_, SubprocessOptions());
  CF_EXPECT(ret == 0,
            "`sso_client` execution failed with combined stdout and stderr: "
                << stdout_ << stderr_);
  CF_EXPECT(std::regex_match(stdout_, kStdoutRegex),
            "Failed parsing `sso_client` output. Output:\n"
                << stdout_);
  std::smatch match;
  std::regex_search(stdout_, match, kStdoutRegex);
  long status_code = std::atol(match[1].str().data());
  std::string body = "";
  if (match.size() == 3) {
    body = match[2];
  }
  return HttpResponse<std::string>{body, status_code};
}
}  // namespace

SsoClient::SsoClient() : exec_cmd_func_(&RunWithManagedStdio) {}

SsoClient::SsoClient(ExecCmdFunc exec_cmd_func)
    : exec_cmd_func_(exec_cmd_func) {}

SsoClient::~SsoClient() {}

Result<HttpResponse<std::string>> SsoClient::GetToString(
    const std::string& url, const std::vector<std::string>& headers) {
  // TODO(b/250670329): Handle request headers.
  CF_EXPECT(headers.empty(), "headers are not handled yet");
  return MakeRequest(exec_cmd_func_, url);
}

Result<HttpResponse<std::string>> SsoClient::PostToString(
    const std::string& url, const std::string& data,
    const std::vector<std::string>& headers) {
  // TODO(b/250670329): Handle request headers.
  CF_EXPECT(headers.empty(), "headers are not handled yet");
  return MakeRequest(exec_cmd_func_, url, HttpMethod::kPost, data);
};

Result<HttpResponse<std::string>> SsoClient::DeleteToString(
    const std::string& url, const std::vector<std::string>& headers) {
  // TODO(b/250670329): Handle request headers.
  CF_EXPECT(headers.empty(), "headers are not handled yet");
  return MakeRequest(exec_cmd_func_, url, HttpMethod::kDelete);
}

Result<HttpResponse<Json::Value>> SsoClient::PostToJson(
    const std::string&, const std::string&, const std::vector<std::string>&) {
  return CF_ERR("Not implemented");
}

Result<HttpResponse<Json::Value>> SsoClient::PostToJson(
    const std::string&, const Json::Value&, const std::vector<std::string>&) {
  return CF_ERR("Not implemented");
}

Result<HttpResponse<std::string>> SsoClient::DownloadToFile(
    const std::string&, const std::string&, const std::vector<std::string>&) {
  return CF_ERR("Not implemented");
}

Result<HttpResponse<Json::Value>> SsoClient::DownloadToJson(
    const std::string&, const std::vector<std::string>&) {
  return CF_ERR("Not implemented");
}

Result<HttpResponse<void>> SsoClient::DownloadToCallback(
    DataCallback, const std::string&, const std::vector<std::string>&) {
  return CF_ERR("Not implemented");
}

Result<HttpResponse<Json::Value>> SsoClient::DeleteToJson(
    const std::string&, const std::vector<std::string>&) {
  return CF_ERR("Not implemented");
}

std::string SsoClient::UrlEscape(const std::string&) { return ""; }

}  // namespace http_client
}  // namespace cuttlefish
