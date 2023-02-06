/*
 * Copyright (C) 2022 The Android Open Source Project
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

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "cvd_server.pb.h"

#include "common/libs/utils/result.h"

namespace cuttlefish {

struct MakeRequestParam {
  std::vector<std::string> cmd_args;
  std::unordered_map<std::string, std::string> env;
  std::vector<std::string> selector_args;
};

cvd::Request MakeRequest(
    const MakeRequestParam& args_and_envs,
    const cvd::WaitBehavior wait_behavior = cvd::WAIT_BEHAVIOR_COMPLETE);

constexpr char kStatusBin[] = "cvd_internal_status";
// The name of environment variable that points to the host out directory
constexpr char kAndroidHostOut[] = "ANDROID_HOST_OUT";
// kAndroidHostOut for old branches
constexpr char kAndroidSoongHostOut[] = "ANDROID_SOONG_HOST_OUT";

Result<std::string> StopBin(const std::string& host_artifacts_path);

template <typename Ostream, typename... Args>
Ostream& ConcatToStream(Ostream& out, Args&&... args) {
  (out << ... << std::forward<Args>(args));
  return out;
}

template <typename... Args>
std::string ConcatToString(Args&&... args) {
  std::stringstream concatenator;
  return ConcatToStream(concatenator, std::forward<Args>(args)...).str();
}

}  // namespace cuttlefish
