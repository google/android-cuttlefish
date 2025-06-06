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

#include "cuttlefish/host/libs/web/http_client/curl_http_client.h"

#include <stdio.h>

#include <curl/curl.h>
#include <curl/easy.h>

#include <functional>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_client_util.h"

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
      LOG(DEBUG) << ScrubSecrets(TrimWhitespace(data, size));
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
      LOG(VERBOSE) << "CURLINFO_END ";
      LOG(DEBUG) << ScrubSecrets(TrimWhitespace(data, size));
      break;
    default:
      LOG(ERROR) << "Unexpected cURL output type: " << type;
      break;
  }
  return 0;
}

size_t curl_to_function_cb(char* ptr, size_t, size_t nmemb, void* userdata) {
  HttpClient::DataCallback* callback = (HttpClient::DataCallback*)userdata;
  if (!(*callback)(ptr, nmemb)) {
    return 0;  // Signals error to curl
  }
  return nmemb;
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
  CurlClient(const bool use_logging_debug_function)
      : use_logging_debug_function_(use_logging_debug_function) {
    curl_ = curl_easy_init();
    if (!curl_) {
      LOG(ERROR) << "failed to initialize curl";
      return;
    }
  }
  ~CurlClient() { curl_easy_cleanup(curl_); }

  Result<HttpResponse<void>> DownloadToCallback(
      HttpMethod method, DataCallback callback, const std::string& url,
      const std::vector<std::string>& headers,
      const std::string& data_to_write) override {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG(DEBUG) << "Downloading '" << url << "'";
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

 private:
  CURL* curl_;
  std::mutex mutex_;
  bool use_logging_debug_function_;
};

}  // namespace

std::unique_ptr<HttpClient> CurlHttpClient(bool use_logging_debug_function) {
  return std::make_unique<CurlClient>(use_logging_debug_function);
}

}  // namespace cuttlefish
