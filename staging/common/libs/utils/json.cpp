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

#include "common/libs/utils/json.h"

namespace cuttlefish {

Result<Json::Value> ParseJson(const std::string& input) {
  Json::Value root;
  JSONCPP_STRING err;
  int raw_len = static_cast<int>(input.length());
  Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  CF_EXPECT(reader->parse(input.c_str(), input.c_str() + raw_len, &root, &err),
            err);
  return root;
}

}  // namespace cuttlefish
