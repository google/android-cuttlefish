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
#include <vector>

#include "cuttlefish/common/libs/fs/fd.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

struct UrlDownload {
  std::string url;
  std::vector<std::string> headers;
  std::optional<std::string> if_range;  // unset when `url` pins one version
  bool resumable = false;  // whether a partial file is worth keeping
  std::optional<uint64_t> size;
};

// Returns whether `path` still names the file `fd` holds open. A download
// that another process finished renames its partial file away, so whoever was
// waiting for the lock on it wakes up holding a file that is gone.
Result<bool> HoldsFileAt(Fd& fd, const std::string& path);

// Writes `download` to `path`, picking up where an interrupted earlier attempt
// left off when the origin allows it. This is the sequential whole-file path;
// random access into a remote zip lives in host/libs/zip/remote_zip.h.
Result<void> DownloadUrlToFile(HttpClient& http_client,
                               const UrlDownload& download,
                               const std::string& path);

}  // namespace cuttlefish
