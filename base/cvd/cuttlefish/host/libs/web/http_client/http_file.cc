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

#include "cuttlefish/host/libs/web/http_client/http_file.h"

#include <stdint.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/fs/shared_fd_stream.h"
#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<HttpResponse<std::string>> HttpGetToFile(
    HttpClient& http_client, const std::string& url, const std::string& path,
    const std::vector<std::string>& headers) {
  VLOG(0) << "Saving '" << url << "' to '" << path << "'";

  std::string temp_path;
  std::unique_ptr<SharedFDOstream> stream;
  uint64_t total_dl = 0;
  uint64_t last_log = 0;
  auto callback = [path, &temp_path, &stream, &total_dl, &last_log](
                      char* data, size_t size) -> bool {
    // On a retry due to a server error, the download will be called from the
    // beginning. The download should be initialized / reset at the nullptr /
    // "beginning of download" case, which can come multiple times.
    if (data == nullptr) {
      if (!temp_path.empty()) {
        if (Result<void> res = RemoveFile(temp_path); !res.ok()) {
          LOG(ERROR) << res.error();
        }
      }
      total_dl = 0;
      last_log = 0;
      Result<std::pair<SharedFD, std::string>> res = SharedFD::Mkostemp(path);
      if (!res.ok()) {
        LOG(ERROR) << "Can't make temp file: " << res.error();
        return false;
      }
      temp_path = res->second;
      stream = std::make_unique<SharedFDOstream>(res->first);
      return !stream->fail();
    }
    total_dl += size;
    if (total_dl / 2 >= last_log) {
      VLOG(0) << "Downloaded " << total_dl << " bytes";
      last_log = total_dl;
    }
    stream->write(data, size);
    return !stream->fail();
  };

  HttpRequest request = {
      .method = HttpMethod::kGet,
      .url = url,
      .headers = std::move(headers),
  };

  HttpResponse<void> http_response =
      CF_EXPECT(http_client.DownloadToCallback(request, callback));

  VLOG(0) << "Downloaded '" << total_dl << "' total bytes from '" << url
          << "' to '" << path << "'.";

  if (http_response.HttpSuccess()) {
    CF_EXPECT(RenameFile(temp_path, path));
  } else {
    CF_EXPECTF(
        RemoveFile(temp_path),
        "Unable to remove temporary file \"{}\"\nMay require manual removal",
        temp_path);
  }
  return HttpResponse<std::string>{
      .data = path,
      .http_code = http_response.http_code,
      .headers = std::move(http_response.headers),
  };
}

}  // namespace cuttlefish
