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

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct UrlDownload {
  std::string url;
  std::vector<std::string> headers;
  // Sent as `If-Range` on a resumed request, so an origin whose object changed
  // answers with the whole of it rather than the tail of something else. Unset
  // for a URL that already names one version of the object.
  std::optional<std::string> if_range;
  // The version the probe found `url` serves, set only where it also serves
  // ranges. Only then is a partial download worth keeping.
  std::optional<std::string> version;
  std::optional<uint64_t> size;
};

// Returns where a download of `version` to `path` keeps its partial file.
std::string PartialFilePath(const std::string& path, std::string_view version);

// Writes `download` to `path`. Where the version and the size are known, it
// reads the object in large ranges through `ZipSourceFromUrl` and picks up
// where an interrupted earlier attempt left off.
Result<void> DownloadUrlToFile(HttpClient& http_client,
                               const UrlDownload& download,
                               const std::string& path);

}  // namespace cuttlefish
