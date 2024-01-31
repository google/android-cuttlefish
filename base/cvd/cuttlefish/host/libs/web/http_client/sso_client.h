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

#pragma once

#include "common/libs/utils/subprocess.h"
#include "host/libs/web/http_client/http_client.h"

namespace cuttlefish {
namespace http_client {

typedef std::function<int(Command&&, const std::string*, std::string*,
                          std::string*, SubprocessOptions)>
    ExecCmdFunc;

class SsoClient : public HttpClient {
 public:
  SsoClient();

  SsoClient(ExecCmdFunc);

  ~SsoClient();

  Result<HttpResponse<std::string>> GetToString(
      const std::string& url,
      const std::vector<std::string>& headers = {}) override;

  Result<HttpResponse<std::string>> PostToString(
      const std::string&, const std::string&,
      const std::vector<std::string>& headers = {}) override;

  Result<HttpResponse<std::string>> DeleteToString(
      const std::string& url,
      const std::vector<std::string>& headers = {}) override;

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string&, const std::string&,
      const std::vector<std::string>& headers = {}) override;

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string&, const Json::Value&,
      const std::vector<std::string>& headers = {}) override;

  Result<HttpResponse<std::string>> DownloadToFile(
      const std::string&, const std::string&,
      const std::vector<std::string>& headers = {}) override;

  Result<HttpResponse<Json::Value>> DownloadToJson(
      const std::string&,
      const std::vector<std::string>& headers = {}) override;

  Result<HttpResponse<void>> DownloadToCallback(
      DataCallback, const std::string&,
      const std::vector<std::string>& headers = {}) override;

  Result<HttpResponse<Json::Value>> DeleteToJson(
      const std::string&,
      const std::vector<std::string>& headers = {}) override;

  std::string UrlEscape(const std::string&) override;

 private:
  ExecCmdFunc exec_cmd_func_;
};

}  // namespace http_client
}  // namespace cuttlefish
