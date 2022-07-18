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

#include "host/libs/web/http_client.h"

#include <stdio.h>

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <android-base/logging.h>
#include <curl/curl.h>
#include <json/json.h>

namespace cuttlefish {
namespace {

size_t curl_to_function_cb(char* ptr, size_t, size_t nmemb, void* userdata) {
  HttpClient::DataCallback* callback = (HttpClient::DataCallback*)userdata;
  if (!(*callback)(ptr, nmemb)) {
    return 0;  // Signals error to curl
  }
  return nmemb;
}

size_t file_write_callback(char* ptr, size_t, size_t nmemb, void* userdata) {
  std::stringstream* stream = (std::stringstream*)userdata;
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

class CurlClient : public HttpClient {
 public:
  CurlClient() {
    curl_ = curl_easy_init();
    if (!curl_) {
      LOG(ERROR) << "failed to initialize curl";
      return;
    }
  }
  ~CurlClient() { curl_easy_cleanup(curl_); }

  HttpResponse<std::string> PostToString(
      const std::string& url, const std::string& data_to_write,
      const std::vector<std::string>& headers) override {
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
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, data_to_write.size());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, data_to_write.c_str());
    std::stringstream data_to_read;
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, file_write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &data_to_read);
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
    return {data_to_read.str(), http_code};
  }

  HttpResponse<Json::Value> PostToJson(
      const std::string& url, const std::string& data_to_write,
      const std::vector<std::string>& headers) override {
    HttpResponse<std::string> response =
        PostToString(url, data_to_write, headers);
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

  HttpResponse<Json::Value> PostToJson(
      const std::string& url, const Json::Value& data_to_write,
      const std::vector<std::string>& headers) override {
    std::stringstream json_str;
    json_str << data_to_write;
    return PostToJson(url, json_str.str(), headers);
  }

  HttpResponse<bool> DownloadToCallback(
      DataCallback callback, const std::string& url,
      const std::vector<std::string>& headers) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG(INFO) << "Attempting to download \"" << url << "\"";
    if (!curl_) {
      LOG(ERROR) << "curl was not initialized\n";
      return {false, -1};
    }
    if (!callback(nullptr, 0)) {  // Signal start of data
      LOG(ERROR) << "Callback failure\n";
      return {false, -1};
    }
    curl_slist* curl_headers = build_slist(headers);
    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_CAINFO,
                     "/etc/ssl/certs/ca-certificates.crt");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, curl_to_function_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &callback);
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
      return {false, -1};
    }
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    return {true, http_code};
  }

  HttpResponse<std::string> DownloadToFile(
      const std::string& url, const std::string& path,
      const std::vector<std::string>& headers) {
    LOG(INFO) << "Attempting to save \"" << url << "\" to \"" << path << "\"";
    std::fstream stream;
    auto callback = [&stream, path](char* data, size_t size) -> bool {
      if (data == nullptr) {
        stream.open(path, std::ios::out | std::ios::binary | std::ios::trunc);
        return !stream.fail();
      }
      stream.write(data, size);
      return !stream.fail();
    };
    auto callback_res = DownloadToCallback(callback, url, headers);
    if (!callback_res.data) {
      return {"", callback_res.http_code};
    }
    return {path, callback_res.http_code};
    std::lock_guard<std::mutex> lock(mutex_);
    if (!curl_) {
      LOG(ERROR) << "curl was not initialized\n";
      return {"", -1};
    }
  }

  HttpResponse<std::string> DownloadToString(
      const std::string& url, const std::vector<std::string>& headers) {
    std::stringstream stream;
    auto callback = [&stream](char* data, size_t size) -> bool {
      if (data == nullptr) {
        stream = std::stringstream();
        return true;
      }
      stream.write(data, size);
      return true;
    };
    auto callback_res = DownloadToCallback(callback, url, headers);
    if (!callback_res.data) {
      return {"", callback_res.http_code};
    }
    return {stream.str(), callback_res.http_code};
  }

  HttpResponse<Json::Value> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers) {
    HttpResponse<std::string> response = DownloadToString(url, headers);
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

  HttpResponse<Json::Value> DeleteToJson(
      const std::string& url,
      const std::vector<std::string>& headers) override {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG(INFO) << "Attempting to download \"" << url << "\"";
    if (!curl_) {
      LOG(ERROR) << "curl was not initialized\n";
      return {"", -1};
    }
    curl_slist* curl_headers = build_slist(headers);
    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(curl_, CURLOPT_CAINFO,
                     "/etc/ssl/certs/ca-certificates.crt");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, curl_headers);
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    std::stringstream data_to_read;
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, file_write_callback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &data_to_read);
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

    auto contents = data_to_read.str();
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
    return {json, http_code};
  }

  std::string UrlEscape(const std::string& text) override {
    char* escaped_str = curl_easy_escape(curl_, text.c_str(), text.size());
    std::string ret{escaped_str};
    curl_free(escaped_str);
    return ret;
  }

 private:
  CURL* curl_;
  std::mutex mutex_;
};

class ServerErrorRetryClient : public HttpClient {
 public:
  ServerErrorRetryClient(HttpClient& inner, int retry_attempts,
                         std::chrono::milliseconds retry_delay)
      : inner_client_(inner),
        retry_attempts_(retry_attempts),
        retry_delay_(retry_delay) {}

  HttpResponse<std::string> PostToString(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers) override {
    return RetryImpl<std::string>(
        [&, this]() { return inner_client_.PostToString(url, data, headers); });
  }

  HttpResponse<Json::Value> PostToJson(
      const std::string& url, const Json::Value& data,
      const std::vector<std::string>& headers) override {
    return RetryImpl<Json::Value>(
        [&, this]() { return inner_client_.PostToJson(url, data, headers); });
  }

  HttpResponse<Json::Value> PostToJson(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers) override {
    return RetryImpl<Json::Value>(
        [&, this]() { return inner_client_.PostToJson(url, data, headers); });
  }

  HttpResponse<std::string> DownloadToFile(
      const std::string& url, const std::string& path,
      const std::vector<std::string>& headers) {
    return RetryImpl<std::string>([&, this]() {
      return inner_client_.DownloadToFile(url, path, headers);
    });
  }

  HttpResponse<std::string> DownloadToString(
      const std::string& url, const std::vector<std::string>& headers) {
    return RetryImpl<std::string>(
        [&, this]() { return inner_client_.DownloadToString(url, headers); });
  }

  HttpResponse<Json::Value> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers) {
    return RetryImpl<Json::Value>(
        [&, this]() { return inner_client_.DownloadToJson(url, headers); });
  }

  HttpResponse<bool> DownloadToCallback(
      DataCallback cb, const std::string& url,
      const std::vector<std::string>& hdrs) override {
    return RetryImpl<bool>([&, this]() {
      return inner_client_.DownloadToCallback(cb, url, hdrs);
    });
  }
  HttpResponse<Json::Value> DeleteToJson(
      const std::string& url,
      const std::vector<std::string>& headers) override {
    return RetryImpl<Json::Value>(
        [&, this]() { return inner_client_.DeleteToJson(url, headers); });
  }

  std::string UrlEscape(const std::string& text) override {
    return inner_client_.UrlEscape(text);
  }

 private:
  template <typename T>
  HttpResponse<T> RetryImpl(std::function<HttpResponse<T>()> attempt_fn) {
    HttpResponse<T> response;
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
  HttpClient& inner_client_;
  int retry_attempts_;
  std::chrono::milliseconds retry_delay_;
};

}  // namespace

/* static */ std::unique_ptr<HttpClient> HttpClient::CurlClient() {
  return std::unique_ptr<HttpClient>(new class CurlClient());
}

/* static */ std::unique_ptr<HttpClient> HttpClient::ServerErrorRetryClient(
    HttpClient& inner, int retry_attempts,
    std::chrono::milliseconds retry_delay) {
  return std::unique_ptr<HttpClient>(
      new class ServerErrorRetryClient(inner, retry_attempts, retry_delay));
}

HttpClient::~HttpClient() = default;
}  // namespace cuttlefish
