//
// Copyright (C) 2026 The Android Open Source Project
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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

// Returns whether `headers` carry an `Authorization` header.
bool HasAuthorization(const std::vector<std::string>& headers);

// Serves one archive over HTTP range requests, as an object store does.
class ZipOverRanges {
 public:
  static Result<ZipOverRanges> Create(
      const std::map<std::string, std::string>& contents,
      bool serve_ranges = true);

  HttpResponse<std::string> operator()(const HttpRequest& request);

  bool RangeRequestMade() const { return *ranged_; }

 private:
  ZipOverRanges(std::string data, bool serve_ranges);

  std::string data_;
  bool serve_ranges_;
  std::shared_ptr<bool> ranged_;
};

}  // namespace cuttlefish
