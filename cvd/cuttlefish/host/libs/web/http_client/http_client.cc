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

#include "host/libs/web/http_client/http_client.h"

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

  Result<HttpResponse<std::string>> GetToString(
      const std::string& url,
      const std::vector<std::string>& headers) override {
    std::stringstream stream;
    auto callback = [&stream](char* data, size_t size) -> bool {
      if (data == nullptr) {
        stream = std::stringstream();
        return true;
      }
      stream.write(data, size);
      return true;
    };
    long http_code = CF_EXPECT(DownloadToCallback(callback, url, headers));
    return HttpResponse<std::string>{stream.str(), http_code};
  }

  Result<HttpResponse<std::string>> PostToString(
      const std::string& url, const std::string& data_to_write,
      const std::vector<std::string>& headers) override {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG(INFO) << "Attempting to download \"" << url << "\"";
    CF_EXPECT(curl_ != nullptr, "curl was not initialized");
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
    CF_EXPECT(res == CURLE_OK,
              "curl_easy_perform() failed. "
                  << "Code was \"" << res << "\". "
                  << "Strerror was \"" << curl_easy_strerror(res) << "\". "
                  << "Error buffer was \"" << error_buf << "\".");
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    return HttpResponse<std::string>{data_to_read.str(), http_code};
  }

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const std::string& data_to_write,
      const std::vector<std::string>& headers) override {
    auto response = CF_EXPECT(PostToString(url, data_to_write, headers));
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
    return HttpResponse<Json::Value>{json, response.http_code};
  }

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const Json::Value& data_to_write,
      const std::vector<std::string>& headers) override {
    std::stringstream json_str;
    json_str << data_to_write;
    return PostToJson(url, json_str.str(), headers);
  }

  Result<long> DownloadToCallback(DataCallback callback, const std::string& url,
                                  const std::vector<std::string>& headers) {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG(INFO) << "Attempting to download \"" << url << "\"";
    CF_EXPECT(curl_ != nullptr, "curl was not initialized");
    CF_EXPECT(callback(nullptr, 0) /* Signal start of data */,
              "callback failure");
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
    CF_EXPECT(res == CURLE_OK,
              "curl_easy_perform() failed. "
                  << "Code was \"" << res << "\". "
                  << "Strerror was \"" << curl_easy_strerror(res) << "\". "
                  << "Error buffer was \"" << error_buf << "\".");
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    return http_code;
  }

  Result<HttpResponse<std::string>> DownloadToFile(
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
    long http_code = CF_EXPECT(DownloadToCallback(callback, url, headers));
    return HttpResponse<std::string>{path, http_code};
  }

  Result<HttpResponse<Json::Value>> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers) {
    auto result = CF_EXPECT(GetToString(url, headers));
    const std::string& contents = result.data;
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
    return HttpResponse<Json::Value>{json, result.http_code};
  }

  Result<HttpResponse<Json::Value>> DeleteToJson(
      const std::string& url,
      const std::vector<std::string>& headers) override {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG(INFO) << "Attempting to download \"" << url << "\"";
    CF_EXPECT(curl_ != nullptr, "curl was not initialized");
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
    CF_EXPECT(res == CURLE_OK,
              "curl_easy_perform() failed. "
                  << "Code was \"" << res << "\". "
                  << "Strerror was \"" << curl_easy_strerror(res) << "\". "
                  << "Error buffer was \"" << error_buf << "\".");
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
    return HttpResponse<Json::Value>{json, http_code};
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

  Result<HttpResponse<std::string>> GetToString(
      const std::string& url, const std::vector<std::string>& headers) {
    auto fn = [&, this]() { return inner_client_.GetToString(url, headers); };
    return CF_EXPECT(RetryImpl<std::string>(fn));
  }

  Result<HttpResponse<std::string>> PostToString(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers) override {
    auto fn = [&, this]() {
      return inner_client_.PostToString(url, data, headers);
    };
    return CF_EXPECT(RetryImpl<std::string>(fn));
  }

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const Json::Value& data,
      const std::vector<std::string>& headers) override {
    auto fn = [&, this]() {
      return inner_client_.PostToJson(url, data, headers);
    };
    return CF_EXPECT(RetryImpl<Json::Value>(fn));
  }

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const std::string& data,
      const std::vector<std::string>& headers) override {
    auto fn = [&, this]() {
      return inner_client_.PostToJson(url, data, headers);
    };
    return CF_EXPECT(RetryImpl<Json::Value>(fn));
  }

  Result<HttpResponse<std::string>> DownloadToFile(
      const std::string& url, const std::string& path,
      const std::vector<std::string>& headers) {
    auto fn = [&, this]() {
      return inner_client_.DownloadToFile(url, path, headers);
    };
    return CF_EXPECT(RetryImpl<std::string>(fn));
  }

  Result<HttpResponse<Json::Value>> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers) {
    auto fn = [&, this]() {
      return inner_client_.DownloadToJson(url, headers);
    };
    return CF_EXPECT(RetryImpl<Json::Value>(fn));
  }

  Result<long> DownloadToCallback(
      DataCallback cb, const std::string& url,
      const std::vector<std::string>& hdrs) override {
    auto fn = [&, this]() { return DownloadToCallbackHelper(cb, url, hdrs); };
    auto response = CF_EXPECT(RetryImpl<bool>(fn));
    return response.http_code;
  }

  Result<HttpResponse<Json::Value>> DeleteToJson(
      const std::string& url,
      const std::vector<std::string>& headers) override {
    auto fn = [&, this]() { return inner_client_.DeleteToJson(url, headers); };
    return CF_EXPECT(RetryImpl<Json::Value>(fn));
  }

  std::string UrlEscape(const std::string& text) override {
    return inner_client_.UrlEscape(text);
  }

 private:
  template <typename T>
  Result<HttpResponse<T>> RetryImpl(
      std::function<Result<HttpResponse<T>>()> attempt_fn) {
    HttpResponse<T> response;
    for (int attempt = 0; attempt != retry_attempts_; ++attempt) {
      if (attempt != 0) {
        std::this_thread::sleep_for(retry_delay_);
      }
      response = CF_EXPECT(attempt_fn());
      if (!response.HttpServerError()) {
        return response;
      }
    }
    return response;
  }

  // Wraps the http_code into an HttpResponse<bool> instance in order to be
  // reused in the RetryImpl function.
  Result<HttpResponse<bool>> DownloadToCallbackHelper(
      DataCallback cb, const std::string& url,
      const std::vector<std::string>& hdrs) {
    long http_code = CF_EXPECT(inner_client_.DownloadToCallback(cb, url, hdrs));
    return HttpResponse<bool>{false /* irrelevant */, http_code};
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
