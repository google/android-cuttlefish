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

#include <sstream>
#include <string>
#include <string_view>

#include <android-base/logging.h>

#include "cuttlefish/common/libs/utils/result.h"
#include "cuttlefish/host/commands/cvd/cli/types.h"

namespace cuttlefish {

// utility struct for std::variant uses
template <typename... Ts>
struct Overload : Ts... {
  using Ts::operator()...;
};

template <typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

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

constexpr android::base::LogSeverity kCvdDefaultVerbosity = android::base::INFO;

Result<android::base::LogSeverity> EncodeVerbosity(
    const std::string& verbosity);

Result<std::string> VerbosityToString(android::base::LogSeverity verbosity);

android::base::LogSeverity SetMinimumVerbosity(android::base::LogSeverity);
Result<android::base::LogSeverity> SetMinimumVerbosity(const std::string&);

android::base::LogSeverity GetMinimumVerbosity();

std::string CvdDir();

std::string PerUserDir();

std::string PerUserCacheDir();

std::string InstanceDatabasePath();

std::string InstanceLocksPath();

std::string DefaultBaseDir();

Result<std::string> GroupDirFromHome(std::string_view group_home_dir);

std::string AssemblyDirFromHome(const std::string& group_home_dir);

// Returns the path to the directory containing the host binaries, shared
// libraries and other files built with the Android build system. It searches,
// in order, the ANDROID_HOST_OUT, ANDROID_SOONG_HOST_OUT and HOME environment
// variables followed by the current directory.
Result<std::string> AndroidHostPath(const cvd_common::Envs& env);

}  // namespace cuttlefish
