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
#include <functional>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <curl/curl.h>
#include <json/json.h>

#include "common/libs/utils/json.h"
#include "common/libs/utils/subprocess.h"
#include "host/libs/web/http_client/http_client_util.h"

namespace cuttlefish {
namespace {

std::string TrimWhitespace(const char* data, const size_t size) {
  std::string converted(data, size);
  return android::base::Trim(converted);
}

int LoggingCurlDebugFunction(CURL*, curl_infotype type, char* data, size_t size,
                             void*) {
  switch (type) {
    case CURLINFO_TEXT:
      LOG(VERBOSE) << "CURLINFO_TEXT ";
      LOG(INFO) << TrimWhitespace(data, size);
      break;
    case CURLINFO_HEADER_IN:
      LOG(VERBOSE) << "CURLINFO_HEADER_IN ";
      LOG(DEBUG) << TrimWhitespace(data, size);
      break;
    case CURLINFO_HEADER_OUT:
      LOG(VERBOSE) << "CURLINFO_HEADER_OUT ";
      LOG(DEBUG) << ScrubSecrets(TrimWhitespace(data, size));
      break;
    case CURLINFO_DATA_IN:
      break;
    case CURLINFO_DATA_OUT:
      break;
    case CURLINFO_SSL_DATA_IN:
      break;
    case CURLINFO_SSL_DATA_OUT:
      break;
    case CURLINFO_END:
      break;
    default:
      LOG(ERROR) << "Unexpected cURL output type: " << type;
      break;
  }
  return 0;
}

enum class HttpMethod {
  kGet,
  kPost,
  kDelete,
};

size_t curl_to_function_cb(char* ptr, size_t, size_t nmemb, void* userdata) {
  HttpClient::DataCallback* callback = (HttpClient::DataCallback*)userdata;
  if (!(*callback)(ptr, nmemb)) {
    return 0;  // Signals error to curl
  }
  return nmemb;
}

Result<std::string> CurlUrlGet(CURLU* url, CURLUPart what, unsigned int flags) {
  char* str_ptr = nullptr;
  CF_EXPECT(curl_url_get(url, what, &str_ptr, flags) == CURLUE_OK);
  std::string str(str_ptr);
  curl_free(str_ptr);
  return str;
}

using ManagedCurlSlist =
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

Result<ManagedCurlSlist> SlistFromStrings(
    const std::vector<std::string>& strings) {
  ManagedCurlSlist curl_headers(nullptr, curl_slist_free_all);
  for (const auto& str : strings) {
    curl_slist* temp = curl_slist_append(curl_headers.get(), str.c_str());
    CF_EXPECT(temp != nullptr,
              "curl_slist_append failed to add \"" << str << "\"");
    (void)curl_headers.release();  // Memory is now owned by `temp`
    curl_headers.reset(temp);
  }
  return curl_headers;
}

class CurlClient : public HttpClient {
 public:
  CurlClient(NameResolver resolver, const bool use_logging_debug_function)
      : resolver_(std::move(resolver)),
        use_logging_debug_function_(use_logging_debug_function) {
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
    return DownloadToString(HttpMethod::kGet, url, headers);
  }

  Result<HttpResponse<std::string>> PostToString(
      const std::string& url, const std::string& data_to_write,
      const std::vector<std::string>& headers) override {
    return DownloadToString(HttpMethod::kPost, url, headers, data_to_write);
  }

  Result<HttpResponse<std::string>> DeleteToString(
      const std::string& url,
      const std::vector<std::string>& headers) override {
    return DownloadToString(HttpMethod::kDelete, url, headers);
  }

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const std::string& data_to_write,
      const std::vector<std::string>& headers) override {
    return DownloadToJson(HttpMethod::kPost, url, headers, data_to_write);
  }

  Result<HttpResponse<Json::Value>> PostToJson(
      const std::string& url, const Json::Value& data_to_write,
      const std::vector<std::string>& headers) override {
    std::stringstream json_str;
    json_str << data_to_write;
    return DownloadToJson(HttpMethod::kPost, url, headers, json_str.str());
  }

  Result<HttpResponse<void>> DownloadToCallback(
      DataCallback callback, const std::string& url,
      const std::vector<std::string>& headers) {
    return DownloadToCallback(HttpMethod::kGet, callback, url, headers);
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
    auto http_response = CF_EXPECT(DownloadToCallback(callback, url, headers));
    return HttpResponse<std::string>{path, http_response.http_code};
  }

  Result<HttpResponse<Json::Value>> DownloadToJson(
      const std::string& url, const std::vector<std::string>& headers) {
    return DownloadToJson(HttpMethod::kGet, url, headers);
  }

  Result<HttpResponse<Json::Value>> DeleteToJson(
      const std::string& url,
      const std::vector<std::string>& headers) override {
    return DownloadToJson(HttpMethod::kDelete, url, headers);
  }

  std::string UrlEscape(const std::string& text) override {
    char* escaped_str = curl_easy_escape(curl_, text.c_str(), text.size());
    std::string ret{escaped_str};
    curl_free(escaped_str);
    return ret;
  }

 private:
  Result<ManagedCurlSlist> ManuallyResolveUrl(const std::string& url_str) {
    if (!resolver_) {
      return ManagedCurlSlist(nullptr, curl_slist_free_all);
    }
    LOG(INFO) << "Manually resolving \"" << url_str << "\"";
    std::stringstream resolve_line;
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(),
                                                            curl_url_cleanup);
    CF_EXPECT(curl_url_set(url.get(), CURLUPART_URL, url_str.c_str(), 0) ==
              CURLUE_OK);
    auto hostname = CF_EXPECT(CurlUrlGet(url.get(), CURLUPART_HOST, 0));
    resolve_line << "+" << hostname;
    auto port =
        CF_EXPECT(CurlUrlGet(url.get(), CURLUPART_PORT, CURLU_DEFAULT_PORT));
    resolve_line << ":" << port << ":";
    resolve_line << android::base::Join(CF_EXPECT(resolver_(hostname)), ",");
    auto slist = CF_EXPECT(SlistFromStrings({resolve_line.str()}));
    return slist;
  }

  Result<HttpResponse<Json::Value>> DownloadToJson(
      HttpMethod method, const std::string& url,
      const std::vector<std::string>& headers,
      const std::string& data_to_write = "") {
    auto response =
        CF_EXPECT(DownloadToString(method, url, headers, data_to_write));
    auto result = ParseJson(response.data);
    if (!result.ok()) {
      Json::Value error_json;
      LOG(ERROR) << "Could not parse json: " << result.error().FormatForEnv();
      error_json["error"] = "Failed to parse json: " + result.error().Message();
      error_json["response"] = response.data;
      return HttpResponse<Json::Value>{error_json, response.http_code};
    }
    return HttpResponse<Json::Value>{*result, response.http_code};
  }

  Result<HttpResponse<std::string>> DownloadToString(
      HttpMethod method, const std::string& url,
      const std::vector<std::string>& headers,
      const std::string& data_to_write = "") {
    std::stringstream stream;
    auto callback = [&stream](char* data, size_t size) -> bool {
      if (data == nullptr) {
        stream = std::stringstream();
        return true;
      }
      stream.write(data, size);
      return true;
    };
    auto http_response = CF_EXPECT(
        DownloadToCallback(method, callback, url, headers, data_to_write));
    return HttpResponse<std::string>{stream.str(), http_response.http_code};
  }

  Result<HttpResponse<void>> DownloadToCallback(
      HttpMethod method, DataCallback callback, const std::string& url,
      const std::vector<std::string>& headers,
      const std::string& data_to_write = "") {
    std::lock_guard<std::mutex> lock(mutex_);
    auto extra_cache_entries = CF_EXPECT(ManuallyResolveUrl(url));
    curl_easy_setopt(curl_, CURLOPT_RESOLVE, extra_cache_entries.get());
    LOG(INFO) << "Attempting to download \"" << url << "\"";
    CF_EXPECT(data_to_write.empty() || method == HttpMethod::kPost,
              "data must be empty for non POST requests");
    CF_EXPECT(curl_ != nullptr, "curl was not initialized");
    CF_EXPECT(callback(nullptr, 0) /* Signal start of data */,
              "callback failure");
    auto curl_headers = CF_EXPECT(SlistFromStrings(headers));
    curl_easy_reset(curl_);
    if (method == HttpMethod::kDelete) {
      curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    curl_easy_setopt(curl_, CURLOPT_CAINFO,
                     "/etc/ssl/certs/ca-certificates.crt");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, curl_headers.get());
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    if (method == HttpMethod::kPost) {
      curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, data_to_write.size());
      curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, data_to_write.c_str());
    }
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, curl_to_function_cb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &callback);
    char error_buf[CURL_ERROR_SIZE];
    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, error_buf);
    curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
    // CURLOPT_VERBOSE must be set for CURLOPT_DEBUGFUNCTION be utilized
    if (use_logging_debug_function_) {
      curl_easy_setopt(curl_, CURLOPT_DEBUGFUNCTION, LoggingCurlDebugFunction);
    }
    CURLcode res = curl_easy_perform(curl_);
    CF_EXPECT(res == CURLE_OK,
              "curl_easy_perform() failed. "
                  << "Code was \"" << res << "\". "
                  << "Strerror was \"" << curl_easy_strerror(res) << "\". "
                  << "Error buffer was \"" << error_buf << "\".");
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);
    return HttpResponse<void>{{}, http_code};
  }

  CURL* curl_;
  NameResolver resolver_;
  std::mutex mutex_;
  bool use_logging_debug_function_;
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

  Result<HttpResponse<std::string>> DeleteToString(
      const std::string& url, const std::vector<std::string>& headers) {
    auto fn = [&, this]() {
      return inner_client_.DeleteToString(url, headers);
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

  Result<HttpResponse<void>> DownloadToCallback(
      DataCallback cb, const std::string& url,
      const std::vector<std::string>& hdrs) override {
    auto fn = [&, this]() {
      return inner_client_.DownloadToCallback(cb, url, hdrs);
    };
    return CF_EXPECT(RetryImpl<void>(fn));
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

 private:
  HttpClient& inner_client_;
  int retry_attempts_;
  std::chrono::milliseconds retry_delay_;
};

}  // namespace

Result<std::vector<std::string>> GetEntDnsResolve(const std::string& host) {
  Command command("/bin/getent");
  command.AddParameter("hosts");
  command.AddParameter(host);

  std::string out;
  std::string err;
  CF_EXPECT(RunWithManagedStdio(std::move(command), nullptr, &out, &err) == 0,
            "`getent hosts " << host << "` failed: out = \"" << out
                             << "\", err = \"" << err << "\"");
  auto lines = android::base::Tokenize(out, "\n");
  for (auto& line : lines) {
    auto line_split = android::base::Tokenize(line, " \t");
    CF_EXPECT(line_split.size() == 2,
              "unexpected line format: \"" << line << "\"");
    line = line_split[0];
  }
  return lines;
}

/* static */ std::unique_ptr<HttpClient> HttpClient::CurlClient(
    NameResolver resolver, bool use_logging_debug_function) {
  return std::unique_ptr<HttpClient>(
      new class CurlClient(std::move(resolver), use_logging_debug_function));
}

/* static */ std::unique_ptr<HttpClient> HttpClient::ServerErrorRetryClient(
    HttpClient& inner, int retry_attempts,
    std::chrono::milliseconds retry_delay) {
  return std::unique_ptr<HttpClient>(
      new class ServerErrorRetryClient(inner, retry_attempts, retry_delay));
}

HttpClient::~HttpClient() = default;

}  // namespace cuttlefish
