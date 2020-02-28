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

#include "curl_wrapper.h"

#include <sstream>
#include <string>
#include <stdio.h>

#include <android-base/logging.h>

#include <curl/curl.h>
#include <json/json.h>

namespace {

size_t file_write_callback(char *ptr, size_t, size_t nmemb, void *userdata) {
  std::stringstream* stream = (std::stringstream*) userdata;
  stream->write(ptr, nmemb);
  return nmemb;
}

curl_slist* build_slist(const std::vector<std::string>& strings) {
  curl_slist* curl_headers = nullptr;
  for (const auto& str : strings) {
    curl_slist* temp = curl_slist_append(curl_headers, str.c_str());
    if (temp == nullptr) {
      LOG(ERROR) << "curl_slist_append failed to add " << str;
      if (curl_headers) {
        curl_slist_free_all(curl_headers);
        return nullptr;
      }
    }
    curl_headers = temp;
  }
  return curl_headers;
}

} // namespace

CurlWrapper::CurlWrapper() {
  curl = curl_easy_init();
  if (!curl) {
    LOG(ERROR) << "failed to initialize curl";
    return;
  }
}

CurlWrapper::~CurlWrapper() {
  curl_easy_cleanup(curl);
}

bool CurlWrapper::DownloadToFile(const std::string& url, const std::string& path) {
  return CurlWrapper::DownloadToFile(url, path, {});
}

bool CurlWrapper::DownloadToFile(const std::string& url, const std::string& path,
                                 const std::vector<std::string>& headers) {
  LOG(INFO) << "Attempting to save \"" << url << "\" to \"" << path << "\"";
  if (!curl) {
    LOG(ERROR) << "curl was not initialized\n";
    return false;
  }
  curl_slist* curl_headers = build_slist(headers);
  curl_easy_reset(curl);
  curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  char error_buf[CURL_ERROR_SIZE];
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  FILE* file = fopen(path.c_str(), "w");
  if (!file) {
    LOG(ERROR) << "could not open file " << path;
    return false;
  }
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*) file);
  CURLcode res = curl_easy_perform(curl);
  if (curl_headers) {
    curl_slist_free_all(curl_headers);
  }
  fclose(file);
  if (res != CURLE_OK) {
    LOG(ERROR) << "curl_easy_perform() failed. "
        << "Code was \"" << res << "\". "
        << "Strerror was \"" << curl_easy_strerror(res) << "\". "
        << "Error buffer was \"" << error_buf << "\".";
    return false;
  }
  return true;
}

std::string CurlWrapper::DownloadToString(const std::string& url) {
  return DownloadToString(url, {});
}

std::string CurlWrapper::DownloadToString(const std::string& url,
                                          const std::vector<std::string>& headers) {
  LOG(INFO) << "Attempting to download \"" << url << "\"";
  if (!curl) {
    LOG(ERROR) << "curl was not initialized\n";
    return "";
  }
  curl_slist* curl_headers = build_slist(headers);
  curl_easy_reset(curl);
  curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  std::stringstream data;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
  char error_buf[CURL_ERROR_SIZE];
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  CURLcode res = curl_easy_perform(curl);
  if (curl_headers) {
    curl_slist_free_all(curl_headers);
  }
  if (res != CURLE_OK) {
    LOG(ERROR) << "curl_easy_perform() failed. "
        << "Code was \"" << res << "\". "
        << "Strerror was \"" << curl_easy_strerror(res) << "\". "
        << "Error buffer was \"" << error_buf << "\".";
    return "";
  }
  return data.str();
}

Json::Value CurlWrapper::DownloadToJson(const std::string& url) {
  return DownloadToJson(url, {});
}

Json::Value CurlWrapper::DownloadToJson(const std::string& url,
                                        const std::vector<std::string>& headers) {
  std::string contents = DownloadToString(url, headers);
  Json::Reader reader;
  Json::Value json;
  if (!reader.parse(contents, json)) {
    LOG(ERROR) << "Could not parse json: " << reader.getFormattedErrorMessages();
    json["error"] = "Failed to parse json.";
    json["response"] = contents;
  }
  return json;
}
