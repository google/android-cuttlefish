//
// Copyright (C) 2020 The Android Open Source Project
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

#include "cuttlefish/common/libs/key_equals_value/key_equals_value.h"

#include <memory>
#include <string>

#include <android-base/logging.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/common/libs/utils/contains.h"
#include "cuttlefish/common/libs/utils/result.h"

namespace cuttlefish {

Result<MiscInfo> ParseMiscInfo(const std::string& misc_info_contents) {
  auto lines = android::base::Split(misc_info_contents, "\n");
  MiscInfo misc_info;
  for (auto& line : lines) {
    line = android::base::Trim(line);
    if (line.empty()) {
      continue;
    }
    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
      LOG(WARNING) << "Line in unknown format: \"" << line << "\"";
      continue;
    }
    // Not using android::base::Split here to only capture the first =
    const auto key = android::base::Trim(line.substr(0, eq_pos));
    const auto value = android::base::Trim(line.substr(eq_pos + 1));
    const bool duplicate = Contains(misc_info, key) && misc_info[key] != value;
    CF_EXPECTF(!duplicate,
               "Duplicate key with different value. key:\"{}\", previous "
               "value:\"{}\", this value:\"{}\"",
               key, misc_info[key], value);
    misc_info[key] = value;
  }
  return misc_info;
}

Result<void> WriteMiscInfo(const MiscInfo& misc_info,
                           const std::string& output_path) {
  std::stringstream file_content;
  for (const auto& entry : misc_info) {
    file_content << entry.first << "=" << entry.second << "\n";
  }

  SharedFD output_file = SharedFD::Creat(output_path.c_str(), 0644);
  CF_EXPECT(output_file->IsOpen(),
            "Failed to open output misc file: " << output_file->StrError());

  CF_EXPECT(
      WriteAll(output_file, file_content.str()) >= 0,
      "Failed to write output misc file contents: " << output_file->StrError());
  return {};
}

}  // namespace cuttlefish
