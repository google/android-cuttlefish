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

#include <sys/types.h>

#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "common/libs/utils/result.h"
#include "cvd_server.pb.h"
#include "host/commands/cvd/types.h"

namespace cuttlefish {

// utility struct for std::variant uses
template <typename... Ts>
struct Overload : Ts... {
  using Ts::operator()...;
};

template <typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

struct MakeRequestForm {
  cvd_common::Args cmd_args;
  cvd_common::Envs env;
  cvd_common::Args selector_args;
  std::optional<std::string> working_dir;
};

cvd::Request MakeRequest(const MakeRequestForm& request_form,
                         const cvd::WaitBehavior wait_behavior);

cvd::Request MakeRequest(const MakeRequestForm& request_form);

// name of environment variable to mark the launch_cvd initiated by the cvd
// server
static constexpr char kCvdMarkEnv[] = "_STARTED_BY_CVD_SERVER_";

constexpr char kServerExecPath[] = "/proc/self/exe";

// The name of environment variable that points to the host out directory
constexpr char kAndroidHostOut[] = "ANDROID_HOST_OUT";
// kAndroidHostOut for old branches
constexpr char kAndroidSoongHostOut[] = "ANDROID_SOONG_HOST_OUT";
constexpr char kAndroidProductOut[] = "ANDROID_PRODUCT_OUT";
constexpr char kLaunchedByAcloud[] = "LAUNCHED_BY_ACLOUD";

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

// given /a/b/c/d/e, ensures
// all directories from /a through /a/b/c/d/e exist
Result<void> EnsureDirectoryExistsAllTheWay(const std::string& dir);

struct InputPathForm {
  /** If nullopt, uses the process' current working dir
   *  But if there is no preceding .. or ., this field is not used.
   */
  std::optional<std::string> current_working_dir;
  /** If nullopt, use SystemWideUserHome()
   *  But, if there's no preceding ~, this field is not used.
   */
  std::optional<std::string> home_dir;
  std::string path_to_convert;
  bool follow_symlink;
};

/**
 * Returns emulated absolute path with a different process'/thread's
 * context.
 *
 * This is useful when daemon(0, 0)-started server process wants to
 * figure out a relative path that came from its client.
 *
 * The call mostly succeeds. It fails only if:
 *  home_dir isn't given so supposed to relies on the local SystemWideUserHome()
 *  but SystemWideUserHome() call fails.
 */
Result<std::string> EmulateAbsolutePath(const InputPathForm& path_info);

}  // namespace cuttlefish
