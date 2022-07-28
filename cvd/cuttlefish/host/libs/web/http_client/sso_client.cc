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

// Matches the sso_client output when it succeeds.
static std::regex stdout_regex(
    "HTTP/\\d+\\.\\d+\\s(\\d+)\\s\\w+\r\n" /* status */
    "(?:.+\r\n)+\r\n"                      /* headers */
    "(.+)?"                                /* body */
    "\n" /* new line added by the sso_client */);

}  // namespace

SsoClient::SsoClient() : exec_cmd_func_(&RunWithManagedStdio) {}

SsoClient::SsoClient(ExecCmdFunc exec_cmd_func)
    : exec_cmd_func_(exec_cmd_func) {}

SsoClient::~SsoClient() {}

Result<HttpResponse<std::string>> SsoClient::GetToString(
    const std::string& url, const std::vector<std::string>&) {
  Command sso_client_cmd(kSsoClientBin);
  sso_client_cmd.AddParameter("--dump_header");
  sso_client_cmd.AddParameter("--url=" + url);
  std::string stdout_, stderr_;
  int ret = exec_cmd_func_(std::move(sso_client_cmd), nullptr, &stdout_,
                           &stderr_, SubprocessOptions());
  CF_EXPECT(ret == 0,
            "`sso_client` execution failed with combined stdout and stderr: "
                << stdout_ << stderr_);
  CF_EXPECT(std::regex_match(stdout_, stdout_regex),
            "Failed parsing `sso_client` output. Output:\n"
                << stdout_);
  std::smatch match;
  std::regex_search(stdout_, match, stdout_regex);
  long status_code = std::atol(match[1].str().data());
  std::string body = "";
  if (match.size() == 3) {
    body = match[2];
  }
  return HttpResponse<std::string>{body, status_code};
}

HttpResponse<std::string> SsoClient::PostToString(
    const std::string&, const std::string&, const std::vector<std::string>&) {
  return {"", 400};
};

HttpResponse<Json::Value> SsoClient::PostToJson(
    const std::string&, const std::string&, const std::vector<std::string>&) {
  return {Json::Value(), 400};
}

HttpResponse<Json::Value> SsoClient::PostToJson(
    const std::string&, const Json::Value&, const std::vector<std::string>&) {
  return {Json::Value(), 400};
}

HttpResponse<std::string> SsoClient::DownloadToFile(
    const std::string&, const std::string&, const std::vector<std::string>&) {
  return {"", 400};
}

HttpResponse<std::string> SsoClient::DownloadToString(
    const std::string&, const std::vector<std::string>&) {
  return {"", 400};
}

HttpResponse<Json::Value> SsoClient::DownloadToJson(
    const std::string&, const std::vector<std::string>&) {
  return {Json::Value(), 400};
}

HttpResponse<bool> SsoClient::DownloadToCallback(
    DataCallback, const std::string&, const std::vector<std::string>&) {
  return {false, 400};
}

HttpResponse<Json::Value> SsoClient::DeleteToJson(
    const std::string&, const std::vector<std::string>&) {
  return {Json::Value(), 400};
}

std::string SsoClient::UrlEscape(const std::string&) { return ""; }

}  // namespace http_client
}  // namespace cuttlefish
