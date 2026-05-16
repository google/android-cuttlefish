/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/truncate.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"

#include "cuttlefish/host/commands/cvd/cli/commands/monitor/ansi_codes.h"

namespace cuttlefish {

TruncatedString TruncateColoredString(std::string_view s,
                                      size_t max_visible_width) {
  std::string result;
  result.reserve(s.size());

  size_t visible_count = 0;
  size_t i = 0;

  while (i < s.size()) {
    if (s.substr(i).starts_with(kAnsiCsi)) {
      size_t end_of_seq = i + 2;
      while (end_of_seq < s.size() && !IsCsiFinalByte(s[end_of_seq])) {
        end_of_seq++;
      }

      if (end_of_seq < s.size()) {
        absl::StrAppend(&result, s.substr(i, end_of_seq - i + 1));
        i = end_of_seq + 1;
        continue;
      }
    }

    if (visible_count < max_visible_width) {
      result.push_back(s[i]);
      visible_count++;
    }
    i++;
  }

  return TruncatedString{
      .string = std::move(result),
      .visible_length = visible_count,
  };
}

}  // namespace cuttlefish
