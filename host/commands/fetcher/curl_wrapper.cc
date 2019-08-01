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

#include <glog/logging.h>

#include <curl/curl.h>
#include <json/json.h>

namespace {

size_t file_write_callback(char *ptr, size_t, size_t nmemb, void *userdata) {
  std::stringstream* stream = (std::stringstream*) userdata;
  stream->write(ptr, nmemb);
  return nmemb;
}

} // namespace

CurlWrapper::CurlWrapper() {
  curl = curl_easy_init();
  if (!curl) {
    LOG(ERROR) << "failed to initialize curl";
    return;
  }
  curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
}

CurlWrapper::~CurlWrapper() {
  curl_easy_cleanup(curl);
}

bool CurlWrapper::DownloadToFile(const std::string& url, const std::string& path) {
  LOG(INFO) << "Attempting to save \"" << url << "\" to \"" << path << "\"";
  if (!curl) {
    LOG(ERROR) << "curl was not initialized\n";
    return false;
  }
  curl_easy_reset(curl);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  FILE* file = fopen(path.c_str(), "w");
  if (!file) {
    LOG(ERROR) << "could not open file " << path;
    return false;
  }
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*) file);
  CURLcode res = curl_easy_perform(curl);
  if(res != CURLE_OK) {
    LOG(ERROR) << "curl_easy_perform() failed: " << curl_easy_strerror(res);
    return false;
  }
  fclose(file);
  return true;
}

std::string CurlWrapper::DownloadToString(const std::string& url) {
  LOG(INFO) << "Attempting to download \"" << url << "\"";
  if (!curl) {
    LOG(ERROR) << "curl was not initialized\n";
    return "";
  }
  curl_easy_reset(curl);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  std::stringstream data;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
  CURLcode res = curl_easy_perform(curl);
  if(res != CURLE_OK) {
    LOG(ERROR) << "curl_easy_perform() failed: " << curl_easy_strerror(res);
    return "";
  }
  return data.str();
}

Json::Value CurlWrapper::DownloadToJson(const std::string& url) {
  std::string contents = DownloadToString(url);
  Json::Reader reader;
  Json::Value json;
  if (!reader.parse(contents, json)) {
    LOG(ERROR) << "Could not parse json: " << reader.getFormattedErrorMessages();
  }
  return json;
}
