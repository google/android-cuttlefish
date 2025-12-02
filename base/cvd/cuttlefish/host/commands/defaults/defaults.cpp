/*
 * Copyright (C) 2025 The Android Open Source Project
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
#include <string.h>
#include <unistd.h>

#include <string_view>

#include <android-base/logging.h>
#include <android-base/macros.h>
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"

#include "cuttlefish/common/libs/key_equals_value/key_equals_value.h"
#include "cuttlefish/host/libs/web/http_client/curl_http_client.h"
#include "cuttlefish/host/libs/web/http_client/http_string.h"

ABSL_FLAG(std::string, filename, "/usr/lib/cuttlefish-common/etc/cf_defaults", "Output filename.");
ABSL_FLAG(std::optional<std::string>, static_defaults_when, std::nullopt,
          "Specify a key-value pair as \"<key>=<value>\". "
          "The key should be a metadata path, e.g., 'project/project-id'. "
          "If found in GCE metadata, then use statically defined defaults.");

namespace cuttlefish {
namespace {

Result<std::string> MetadataValue(std::string_view key) {
  std::string url = absl::StrFormat(
      "http://metadata.google.internal/computeMetadata/v1/%s", key);
  const auto client = CurlHttpClient();
  CF_EXPECT(client.get());
  auto response =
      CF_EXPECT(HttpGetToString(*client, url, {"Metadata-Flavor: Google"}));
  CF_EXPECT(response.HttpSuccess());
  return response.data;
}

Result<std::pair<std::string, std::string>> ParseKeyValueFlag(
    const std::string &flag) {
  std::map<std::string, std::string> kvs = CF_EXPECT(
      ParseKeyEqualsValue(flag));
  CF_EXPECT(kvs.size() == 1);
  return *(kvs.begin());
}

/*
 * Use static defaults when the flag is specified with a key-value pair
 * that exists in GCE metadata.
 *
 * For example, if the flag value is `project/project-id=testing`,
 * static defaults will only be used if running in the `testing` project.
 */
Result<bool> UseStaticDefaults(const std::optional<std::string> &flag) {
  if (!flag.has_value()) {
    LOG(INFO) << "Will not use static defaults.";
    return false;
  }

  /* Is the flag value well-formed? */
  LOG(INFO) << "Looking for metadata value from flag " << flag.value();
  Result<std::pair<std::string, std::string>> kv =
      CF_EXPECT(ParseKeyValueFlag(flag.value()),
                "Couldn't parse key-value pair to find in metadata, "
                "got: " << flag.value());

  /* Does the key exist? If so, then get the value. */
  std::string actual =
      CF_EXPECT(MetadataValue(kv->first),
                "Couldn't get value at metadata path " << kv->first);
  std::string expected = kv->second;

  /* Is the value expected? */ 
  if (actual != expected) {
    // No error, but do not use static defaults.
    LOG(WARNING) << absl::StrFormat(
      "Metadata value for %s unexpected, got: '%s', expected '%s'",
      kv->first, actual, expected);
  }
  return actual == expected;
}

Result<std::map<std::string, std::string>> DefaultsFromMetadata() {
  std::string data =
      CF_EXPECT(MetadataValue("instance/attributes/cf-defaults"));
  return ParseKeyEqualsValue(data);
}

std::map<std::string, std::string> StaticDefaults() {
  return {
      {"use_cvdalloc", "true"},
  };
}

}  // namespace

Result<int> DefaultsMain(int argc, char *argv[]) {
  std::vector<char *> args = absl::ParseCommandLine(argc, argv);

  std::string filename = absl::GetFlag(FLAGS_filename);
  LOG(INFO) << "Writing to " << filename;

  if (CF_EXPECT(UseStaticDefaults(absl::GetFlag(FLAGS_static_defaults_when)))) {
    std::map<std::string, std::string> m = StaticDefaults();
    CF_EXPECT(WriteKeyEqualsValue(m, filename));
    return 0;
  }

  Result<std::map<std::string, std::string>> m = DefaultsFromMetadata();
  if (!m.ok()) {
    LOG(INFO) << "Couldn't get defaults from metadata.";
    // Not necessarily an error, so don't report it.
    return 0;
  }

  CF_EXPECT(WriteKeyEqualsValue(*m, filename));
  return 0;
}

}  // namespace cuttlefish

int main(int argc, char *argv[]) {
  auto res = cuttlefish::DefaultsMain(argc, argv);
  if (!res.ok()) {
    LOG(ERROR) << "defaults failed: \n" << res.error().FormatForEnv();
    abort();
  }

  return *res;
}
