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

#include "cuttlefish/common/libs/utils/semver.h"

#include <regex>

#include "absl/strings/numbers.h"

namespace cuttlefish {

Result<SemVer> ParseSemVer(std::string_view str) {
  // From
  // https://semver.org/#is-there-a-suggested-regular-expression-regex-to-check-a-semver-string:
  std::regex semver_regex(
      R"(^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-((?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$)");

  std::string_view::const_iterator begin = str.begin();
  std::string_view::const_iterator end = str.end();
  std::match_results<std::string_view::const_iterator> matches;
  CF_EXPECT(std::regex_match(begin, end, matches, semver_regex),
            "Failed to parse semver from " << str);

  CF_EXPECT_EQ(matches.size(), 6);

  SemVer semver;
  CF_EXPECT(absl::SimpleAtoi(matches[1].str(), &semver.major),
            "Failed to parse int from " << matches[1]);
  CF_EXPECT(absl::SimpleAtoi(matches[2].str(), &semver.minor),
            "Failed to parse int from " << matches[2]);
  CF_EXPECT(absl::SimpleAtoi(matches[3].str(), &semver.patch),
            "Failed to parse int from " << matches[3]);
  semver.prerelease = matches[4].str();
  semver.build_metadata = matches[5].str();
  return semver;
}

}  // namespace cuttlefish
