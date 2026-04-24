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

#include "cuttlefish/host/libs/metrics/github_environment.h"

#include <optional>
#include <string>
#include <string_view>

#include "cuttlefish/common/libs/utils/environment.h"

namespace cuttlefish {
namespace {

// https://docs.github.com/en/actions/reference/workflows-and-actions/variables
// set to "true" when GitHub Actions is running the workflow
constexpr std::string_view kGitHubActions = "GITHUB_ACTIONS";
// holds the owner and repository name
constexpr std::string_view kGitHubRepository = "GITHUB_REPOSITORY";

constexpr std::string_view kAndroidCuttlefish = "google/android-cuttlefish";
constexpr std::string_view kCloudAndroidOrchestration =
    "google/cloud-android-orchestration";

GitHubRepository ToGitHubRepository(std::string_view gh_env_str) {
  if (gh_env_str == kAndroidCuttlefish) {
    return GitHubRepository::AndroidCuttlefish;
  } else if (gh_env_str == kCloudAndroidOrchestration) {
    return GitHubRepository::CloudAndroidOrchestration;
  } else {
    return GitHubRepository::Unknown;
  }
}

}  // namespace

std::optional<GitHubRepository> DetectGitHubRepository() {
  if (StringFromEnv(std::string(kGitHubActions)) != "true") {
    return std::nullopt;
  }
  return ToGitHubRepository(
      StringFromEnv(std::string(kGitHubRepository), "unknown"));
}

}  // namespace cuttlefish
