//
// Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace cuttlefish {

enum class SafeLevel {
  kSafeLevelUnspecified = 0,
  kSafe0,
  kSafe1,
  kSafe2,
  kPlatinum,
};

std::string_view format_as(SafeLevel level);

template <typename Sink>
void AbslStringify(Sink& sink, SafeLevel level) {
  sink.Append(format_as(level));
}

std::ostream& operator<<(std::ostream& os, SafeLevel level);

inline constexpr SafeLevel kAllSafeLevels[] = {
    SafeLevel::kPlatinum,
    SafeLevel::kSafe2,
    SafeLevel::kSafe1,
    SafeLevel::kSafe0,
    SafeLevel::kSafeLevelUnspecified,
};

inline constexpr char kAndroidBuildServiceUrl[] =
    "https://androidbuild-pa.googleapis.com/v4";

class AndroidBuildUrl {
 public:
  AndroidBuildUrl(std::string api_base_url, std::string api_key,
                  std::string project_id);

  std::string GetLatestBuildIdUrl(
      std::string_view branch, std::string_view target,
      SafeLevel safe_level = SafeLevel::kSafeLevelUnspecified);
  std::string GetBuildUrl(std::string_view id, std::string_view target);
  std::string GetArtifactUrl(std::string_view id, std::string_view target,
                             const std::vector<std::string>& artifact_filenames,
                             std::string_view page_token);
  std::string GetArtifactDownloadUrl(std::string_view id,
                                     std::string_view target,
                                     std::string_view artifact);

 private:
  std::string api_base_url_;
  std::string api_key_;
  std::string project_id_;
};

}  // namespace cuttlefish
