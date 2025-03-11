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

#include "host/commands/fetcher/curl_wrapper.h"

#include <stdio.h>

#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <android-base/logging.h>
#include <curl/curl.h>
#include <json/json.h>

namespace cuttlefish {
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

class CurlWrapperImpl : public CurlWrapper {
 public:
  CurlWrapperImpl() {
    curl_ = curl_easy_init();
    if (!curl_) {
      LOG(ERROR) << "failed to initialize curl";
      return;
    }
  }
  ~CurlWrapperImpl() { curl_easy_cleanup(curl_); }

  CurlResponse<std::string> DownloadToFile(
      const std::string& url, const std::string& path,
      const std::vector<std::string>& headers) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG(INFO) << "Attempting to save \"" << url << "\" to \"" << path << "\"";
    if (!curl_) {
      LOG(ERROR) << "curl was not initialized\n";
      return {"", -1};
    }
    curl_slist* curl_headers = build_slist(headers);
    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_CAINFO,
                     "/etc/ssl/certs/ca-certificates.crt");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    char error_buf[CURL_ERROR_SIZE];
    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, error_buf);
    curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
    FILE* file = fopen(path.c_str(), "w");
    if (!file) {
      LOG(ERROR) << "could not open file " << path;
      return {"", -1};
    }
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, (void*)file);
    CURLcode res = curl_easy_perform(curl_);
    if (curl_headers) {
      curl_slist_free_all(curl_headers);
    }
    fclose(file);
    if (res != CURLE_OK) {
      LOG(ERROR) << "curl_easy_perform() failed. "
                 << "Code was \"" << res << "\". "
                 << "Strerror was \"" << curl_easy_strerror(res) << "\". "
                 << "Error buffer was \"" << error_buf << "\".";
      return {{}, -1};
    }
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    return {path, http_code};
  }

  CurlResponse<std::string> DownloadToString(
      const std::string& url, const std::vector<std::string>& headers) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG(INFO) << "Attempting to download \"" << url << "\"";
    if (!curl_) {
      LOG(ERROR) << "curl was not initialized\n";
      return {"", -1};
    }
    curl_slist* curl_headers = build_slist(headers);
    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_CAINFO,
                     "/etc/ssl/certs/ca-certificates.crt");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    std::stringstream data;
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, file_write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &data);
    char error_buf[CURL_ERROR_SIZE];
    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, error_buf);
    curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
    CURLcode res = curl_easy_perform(curl_);
    if (curl_headers) {
      curl_slist_free_all(curl_headers);
    }
    if (res != CURLE_OK) {
      LOG(ERROR) << "curl_easy_perform() failed. "
                 << "Code was \"" << res << "\". "
                 << "Strerror was \"" << curl_easy_strerror(res) << "\". "
                 << "Error buffer was \"" << error_buf << "\".";
      return {"", -1};
    }
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    return {data.str(), http_code};
  }

  CurlResponse<Json::Value> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers) {
    CurlResponse<std::string> response = DownloadToString(url, headers);
    const std::string& contents = response.data;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value json;
    std::string errorMessage;
    if (!reader->parse(&*contents.begin(), &*contents.end(), &json,
                       &errorMessage)) {
      LOG(ERROR) << "Could not parse json: " << errorMessage;
      json["error"] = "Failed to parse json.";
      json["response"] = contents;
    }
    return {json, response.http_code};
  }

 private:
  CURL* curl_;
  std::mutex mutex_;
};

class CurlServerErrorRetryingWrapper : public CurlWrapper {
 public:
  CurlServerErrorRetryingWrapper(CurlWrapper& inner, int retry_attempts,
                                 std::chrono::milliseconds retry_delay)
      : inner_curl_(inner),
        retry_attempts_(retry_attempts),
        retry_delay_(retry_delay) {}

  CurlResponse<std::string> DownloadToFile(
      const std::string& url, const std::string& path,
      const std::vector<std::string>& headers) {
    return RetryImpl<std::string>(
        [&, this]() { return inner_curl_.DownloadToFile(url, path, headers); });
  }

  CurlResponse<std::string> DownloadToString(
      const std::string& url, const std::vector<std::string>& headers) {
    return RetryImpl<std::string>(
        [&, this]() { return inner_curl_.DownloadToString(url, headers); });
  }

  CurlResponse<Json::Value> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers) {
    return RetryImpl<Json::Value>(
        [&, this]() { return inner_curl_.DownloadToJson(url, headers); });
  }

 private:
  template <typename T>
  CurlResponse<T> RetryImpl(std::function<CurlResponse<T>()> attempt_fn) {
    CurlResponse<T> response;
    for (int attempt = 0; attempt != retry_attempts_; ++attempt) {
      if (attempt != 0) {
        std::this_thread::sleep_for(retry_delay_);
      }
      response = attempt_fn();
      if (!response.HttpServerError()) {
        return response;
      }
    }
    return response;
  }

 private:
  CurlWrapper& inner_curl_;
  int retry_attempts_;
  std::chrono::milliseconds retry_delay_;
};

}  // namespace

/* static */ std::unique_ptr<CurlWrapper> CurlWrapper::Create() {
  return std::unique_ptr<CurlWrapper>(new CurlWrapperImpl());
}

/* static */ std::unique_ptr<CurlWrapper> CurlWrapper::WithServerErrorRetry(
    CurlWrapper& inner, int retry_attempts,
    std::chrono::milliseconds retry_delay) {
  return std::unique_ptr<CurlWrapper>(
      new CurlServerErrorRetryingWrapper(inner, retry_attempts, retry_delay));
}

CurlWrapper::~CurlWrapper() = default;
}
