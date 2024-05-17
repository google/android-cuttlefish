//
// Copyright (C) 2023 The Android Open Source Project
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

#include "host/libs/web/http_client/http_client_util.h"

#include <regex>
#include <string>

namespace cuttlefish {

std::string ScrubSecrets(const std::string& data) {
  std::string result = data;
  // eg [<head>]Authorization: Bearer token_text[<tail>] ->
  //    [<head>]Authorization: Bearer token_...[<tail>]
  result = std::regex_replace(
      result, std::regex("(.*)([Aa]uthorization:[ ]+\\S+[ ]+)(\\S{6})\\S*"),
      "$1$2$3...");
  // eg [<head>]client_secret=token_text[<tail>] ->
  //    [<head>]client_secret=token_...[<tail>]
  result = std::regex_replace(
      result, std::regex("(client_secret=)(\\S{6})[^\\&\\s]*"), "$1$2...");
  return result;
}

}  // namespace cuttlefish
