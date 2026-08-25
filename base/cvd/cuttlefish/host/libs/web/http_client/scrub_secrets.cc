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

#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"

#include <regex>
#include <string>
#include <string_view>

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
  // eg [<head>]GET /path?signature=token_text HTTP/1.1[<tail>] ->
  //    [<head>]GET /path?... HTTP/1.1[<tail>]
  // Any query string is redacted, so this also covers an absolute URL in a
  // header value such as the Location of a redirect, or in a JSON response
  // body, and does not depend on a request line ending in " HTTP/". A '"'
  // ends the match so a redacted JSON string stays closed.
  result =
      std::regex_replace(result, std::regex("\\?[^ \\t\\r\\n\"]*"), "?...");
  return result;
}

std::string ScrubUrl(std::string_view url) {
  return std::string(url.substr(0, url.find_first_of("?#")));
}

}  // namespace cuttlefish
