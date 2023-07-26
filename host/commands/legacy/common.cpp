/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "host/commands/legacy/common.h"

#include <unistd.h>

#include <string>
#include <string_view>
#include <vector>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>

namespace cuttlefish {
namespace {

std::string TurnIntoCvd(std::string str, const std::string& suffix) {
  std::string_view sv(str);
  CHECK(android::base::ConsumeSuffix(&sv, suffix))
      << str << " doesn't end with " << suffix;
  return std::string(sv) + "cvd";
}

}  // namespace
void ExecCvdFromLegacy(const std::string& legacy_cmd, int argc, char** argv,
                       const std::vector<std::string>& extra_args) {
  auto exec_dir = android::base::GetExecutableDirectory();
  auto binary = exec_dir + "/cvd";
  CHECK(argc > 0) << "Expected at least argv[0] to be initialized";
  auto arg0 = TurnIntoCvd(argv[0], legacy_cmd);
  std::vector<const char*> new_args;
  new_args.push_back(arg0.c_str());
  for (const auto& a : extra_args) {
    new_args.push_back(a.c_str());
  }
  for (int i = 1; i < argc; ++i) {
    new_args.push_back(argv[i]);
  }
  new_args.push_back(nullptr);
  auto res = execv(binary.c_str(), const_cast<char**>(new_args.data()));
  auto err = errno;
  LOG(FATAL) << "execv failed with return value " << res
             << " and error: " << strerror(err);
}

}  // namespace cuttlefish
