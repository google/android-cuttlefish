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

#include "cuttlefish/host/libs/web/url_download.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/types.h>

#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_file.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/posix/remove.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

Result<void> FullDownload(HttpClient& http_client, const UrlDownload& download,
                          const std::string& path) {
  HttpResponse<std::string> response = CF_EXPECT(
      HttpGetToFile(http_client, download.url, path, download.headers));
  CF_EXPECTF(response.HttpSuccess(), "'{}' - {}:{}", ScrubUrl(download.url),
             response.http_code, response.StatusDescription());
  return {};
}

}  // namespace

Result<void> DownloadUrlToFile(HttpClient& http_client,
                               const UrlDownload& download,
                               const std::string& path) {
  // Without something to resume against, a unique temporary file per attempt
  // keeps concurrent downloads of the same artifact out of each other's way.
  if (!download.resumable) {
    CF_EXPECT(FullDownload(http_client, download, path));
    return {};
  }

  const std::string part_path = absl::StrCat(path, ".part");
  SharedFD part = SharedFD::Open(part_path, O_RDWR | O_CREAT, 0644);
  CF_EXPECTF(part->IsOpen(), "Could not open '{}' - {}", part_path,
             part->StrError());
  CF_EXPECTF(part->Flock(LOCK_EX), "Could not lock '{}'", part_path);

  off_t have = part->LSeek(0, SEEK_END);
  CF_EXPECTF(have >= 0, "Could not measure '{}' - {}", part_path,
             part->StrError());

  uint64_t offset = 0;
  if (download.size.has_value() && have > 0 &&
      static_cast<uint64_t>(have) < *download.size) {
    offset = have;
  }

  while (true) {
    HttpRequest request = {
        .method = HttpMethod::kGet,
        .url = download.url,
        .headers = download.headers,
    };
    if (offset > 0) {
      request.headers.push_back(fmt::format("Range: bytes={}-", offset));
      if (!download.if_range.empty()) {
        request.headers.push_back(
            absl::StrCat("If-Range: ", download.if_range));
      }
    }

    uint64_t written = 0;
    auto callback = [&part, &part_path, offset, &written](char* data,
                                                          size_t size) -> bool {
      // A retry inside the client starts the response over from the range that
      // was asked for.
      if (data == nullptr) {
        written = 0;
        if (Result<void> truncated = part->Truncate(offset);
            !truncated.has_value()) {
          LOG(ERROR) << truncated.error();
          return false;
        }
        return part->LSeek(offset, SEEK_SET) >= 0;
      }
      if (WriteAll(part, data, size) != static_cast<ssize_t>(size)) {
        LOG(ERROR) << "Could not write '" << part_path
                   << "': " << part->StrError();
        return false;
      }
      written += size;
      return true;
    };

    HttpResponse<void> response =
        CF_EXPECT(http_client.DownloadToCallback(request, callback));
    if (!response.HttpSuccess()) {
      // The body of an error response is not the artifact.
      CF_EXPECT(RemoveFile(part_path));
      return CF_ERRF("'{}' - {}:{}", ScrubUrl(download.url), response.http_code,
                     response.StatusDescription());
    }
    if (offset == 0 || !download.size.has_value() ||
        offset + written == *download.size) {
      break;
    }
    LOG(WARNING) << "'" << ScrubUrl(download.url)
                 << "' answered a resumed request with the whole object";
    offset = 0;
  }

  CF_EXPECT(RenameFile(part_path, path));
  return {};
}

}  // namespace cuttlefish
