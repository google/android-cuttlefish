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

#pragma once

#include <string>

#include <curl/curl.h>
#include <json/json.h>

namespace cuttlefish {

class CurlWrapper {
  CURL* curl;
public:
  CurlWrapper();
  ~CurlWrapper();
  CurlWrapper(const CurlWrapper&) = delete;
  CurlWrapper& operator=(const CurlWrapper*) = delete;
  CurlWrapper(CurlWrapper&&) = default;

  bool DownloadToFile(const std::string& url, const std::string& path);
  bool DownloadToFile(const std::string& url, const std::string& path,
                      const std::vector<std::string>& headers);
  std::string DownloadToString(const std::string& url);
  std::string DownloadToString(const std::string& url,
                               const std::vector<std::string>& headers);
  Json::Value DownloadToJson(const std::string& url);
  Json::Value DownloadToJson(const std::string& url,
                             const std::vector<std::string>& headers);
};

}
