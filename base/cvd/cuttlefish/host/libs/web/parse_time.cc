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

#include "cuttlefish/host/libs/web/parse_time.h"

#include <time.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

Result<std::chrono::system_clock::time_point> ParseTime(std::string_view str) {
  std::stringstream stream = std::stringstream(std::string(str));
  tm time_tm = {};
  stream >> std::get_time(&time_tm, "%Y-%m-%dT%H:%M:%S");
  CF_EXPECTF(!!stream, "Failed to parse time '{}'", str);

  return std::chrono::system_clock::from_time_t(mktime(&time_tm));
}

}  // namespace cuttlefish
