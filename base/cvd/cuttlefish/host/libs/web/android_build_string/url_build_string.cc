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

#include "cuttlefish/host/libs/web/android_build_string/url_build_string.h"

#include <stddef.h>

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/strip.h"
#include "fmt/ostream.h"

#include "cuttlefish/host/libs/web/android_build_string/parse_filepath.h"
#include "cuttlefish/host/libs/web/http_client/scrub_secrets.h"
#include "cuttlefish/result/expect.h"
#include "cuttlefish/result/result_type.h"

namespace cuttlefish {

namespace {

// Returns the digest of a "#sha256=<64 hex digits>" fragment, or nullopt when
// `fragment` is anything else.
std::optional<std::string_view> Sha256FragmentDigest(
    std::string_view fragment) {
  constexpr size_t kHexDigits = 64;
  if (!absl::ConsumePrefix(&fragment, "#sha256=") ||
      fragment.size() != kHexDigits ||
      fragment.find_first_not_of("0123456789abcdefABCDEF") !=
          std::string_view::npos) {
    return std::nullopt;
  }
  return fragment;
}

// A URL build string's '{selector}', when it has one, must be complete and
// must end the string. Checked before `ParseFilepath`, which allows a
// selector anywhere and discards whatever follows it.
bool HasSelectorSuffix(std::string_view build_string) {
  const size_t open_bracket = build_string.find('{');
  const size_t close_bracket = build_string.find('}');
  if (open_bracket == std::string_view::npos) {
    return close_bracket == std::string_view::npos;
  }
  return close_bracket + 1 == build_string.size() &&
         close_bracket > open_bracket + 1;
}

// A fragment is a suffix by definition, so a '#' anywhere else is an error
// rather than something silently kept in the URL. What follows the '#' is not
// quoted back, as it can hold a query string.
Result<std::pair<std::string_view, std::optional<std::string_view>>>
ParseSha256Fragment(std::string_view build_string) {
  const size_t fragment_start = build_string.find('#');
  if (fragment_start == std::string_view::npos) {
    return {{build_string, std::nullopt}};
  }
  const std::optional<std::string_view> digest =
      Sha256FragmentDigest(build_string.substr(fragment_start));
  CF_EXPECTF(digest.has_value(),
             "The only fragment a URL build string may carry is a trailing "
             "'#sha256=<64 hex digits>'.  Input: '{}'",
             ScrubUrl(build_string));
  return {{build_string.substr(0, fragment_start), digest}};
}

}  // namespace

Result<UrlBuildString> ParseUrlBuildString(std::string_view scheme,
                                           std::string_view build_string) {
  CF_EXPECTF(scheme == "gs" || scheme == "https",
             "Unsupported URL scheme '{}'.  The supported URL schemes are "
             "'gs://' and 'https://' (cleartext 'http://' is not).  Input: "
             "'{}'",
             scheme, ScrubUrl(build_string));
  // The format reserves ',' because build strings travel in comma separated
  // lists, where a URL holding one would be split into pieces that each parse
  // as some other kind of build string.
  CF_EXPECTF(build_string.find(',') == std::string_view::npos,
             "URL build strings cannot contain a comma, which the format "
             "reserves because build strings travel in comma separated "
             "lists.  Input: '{}'",
             ScrubUrl(build_string));

  auto [without_fragment, sha256] =
      CF_EXPECT(ParseSha256Fragment(build_string));
  CF_EXPECTF(HasSelectorSuffix(without_fragment),
             "A URL build string ends either with its '{{selector}}' or with "
             "the URL itself.  Input: '{}'",
             ScrubUrl(build_string));
  auto [url, filepath] = CF_EXPECT(ParseFilepath(without_fragment));

  const size_t query = url.find('?');
  const bool object_form = !url.substr(0, query).ends_with('/');
  // The directory form resolves an artifact by joining its name onto the URL,
  // which would put the name after the query string. A signed query also
  // authorizes exactly one resource, so it cannot cover a listing and every
  // artifact under the prefix.
  CF_EXPECTF(object_form || query == std::string::npos,
             "Query strings are only supported on URLs naming an object, not "
             "on a '/'-terminated directory.  Input: '{}'",
             ScrubUrl(build_string));
  CF_EXPECTF(object_form || !sha256.has_value(),
             "'#sha256=' is only supported on URLs naming an object, not on a "
             "'/'-terminated directory.  Input: '{}'",
             ScrubUrl(build_string));

  std::optional<std::string> digest;
  if (sha256.has_value()) {
    digest = std::string(*sha256);
  }
  return UrlBuildString{.url = url, .filepath = filepath, .sha256 = digest};
}

std::ostream& operator<<(std::ostream& out,
                         const UrlBuildString& build_string) {
  fmt::print(out, "(url=\"{}\", filepath=\"{}\", sha256=\"{}\")",
             ScrubUrl(build_string.url), build_string.filepath.value_or(""),
             build_string.sha256.value_or(""));
  return out;
}

}  // namespace cuttlefish
