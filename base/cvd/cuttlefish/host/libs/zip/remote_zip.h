//
// Copyright (C) 2025 The Android Open Source Project
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

#include <string>
#include <vector>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/libs/web/http_client/http_client.h"
#include "cuttlefish/host/libs/zip/zip_cc.h"

namespace cuttlefish {

/* Creates a read-only zip archive that downloads files on-demand from a remote
 * URL. It assumes the remote web server supports HTTP range requests and
 * requires knowing the size of the remote file. `headers` are passed through
 * when making HTTP requests to the `HttpClient`. */
Result<ReadableZip> ZipFromUrl(HttpClient&, const std::string& url,
                               uint64_t size, std::vector<std::string> headers);
}
