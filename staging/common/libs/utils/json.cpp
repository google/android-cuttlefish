//
// Copyright (C) 2022 The Android Open Source Project
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

#include <memory>
#include <string>
#include <string_view>

#include "common/libs/fs/shared_buf.h"
#include "common/libs/fs/shared_fd.h"
#include "common/libs/utils/json.h"

namespace cuttlefish {

Result<Json::Value> ParseJson(std::string_view input) {
  Json::Value root;
  JSONCPP_STRING err;
  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  auto begin = input.data();
  auto end = begin + input.length();
  CF_EXPECT(reader->parse(begin, end, &root, &err), err);
  return root;
}

Result<Json::Value> LoadFromFile(SharedFD json_fd) {
  CF_EXPECT(json_fd->IsOpen(), "json_fd is not open.");
  std::string json_contents;
  // on success, this must return a positive integer
  CF_EXPECT_GE(ReadAll(json_fd, &json_contents), 0,
               "ReadAll() failed and returned 0 or -1");
  Json::Value json_value = CF_EXPECTF(
      ParseJson(json_contents), "Failed to parse json: \n{}", json_contents);
  return json_value;
}

Result<Json::Value> LoadFromFile(const std::string& path_to_file) {
  SharedFD json_fd = SharedFD::Open(path_to_file, O_RDONLY);
  auto json_value =
      CF_EXPECTF(LoadFromFile(json_fd), "Failed to open {}", path_to_file);
  return json_value;
}

}  // namespace cuttlefish
