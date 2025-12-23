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
#include <sstream>
#include <string>
#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/fs/shared_buf.h"
#include "cuttlefish/common/libs/fs/shared_fd.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {

Result<std::map<std::string, std::string>> ParseKeyEqualsValue(
    const std::string& contents) {
  std::map<std::string, std::string> key_equals_value;
  for (std::string_view line : absl::StrSplit(contents, '\n')) {
    std::pair<std::string_view, std::string_view> key_value =
        absl::StrSplit(line, absl::MaxSplits('=', 1));
    key_value.first = absl::StripAsciiWhitespace(key_value.first);

    if (key_value.first.empty()) {
      continue;
    }

    key_value.second = absl::StripAsciiWhitespace(key_value.second);

    auto [it, inserted] = key_equals_value.emplace(key_value);
    CF_EXPECTF(inserted || it->second == key_value.second,
               "Duplicate key with different value. key:\"{}\", previous "
               "value:\"{}\", this value:\"{}\"",
               key_value.first, it->second, key_value.second);
  }
  return key_equals_value;
}

std::string SerializeKeyEqualsValue(
    const std::map<std::string, std::string>& key_equals_value) {
  std::stringstream file_content;
  for (const auto& [key, value] : key_equals_value) {
    file_content << key << "=" << value << "\n";
  }
  return file_content.str();
}

Result<void> WriteKeyEqualsValue(
    const std::map<std::string, std::string>& key_equals_value,
    const std::string& path) {
  SharedFD output = SharedFD::Creat(path, 0644);
  CF_EXPECTF(output->IsOpen(), "Failed to open '{}': '{}'", path,
             output->StrError());

  std::string serialized = SerializeKeyEqualsValue(key_equals_value);

  CF_EXPECTF(WriteAll(output, serialized) == serialized.size(),
             "Failed to write to '{}': '{}'", path, output->StrError());

  return {};
}

}  // namespace cuttlefish
