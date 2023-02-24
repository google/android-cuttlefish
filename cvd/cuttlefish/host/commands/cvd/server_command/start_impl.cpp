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

#include "host/commands/cvd/server_command/start_impl.h"

#include <fstream>
#include <regex>

#include <android-base/strings.h>

#include "host/commands/cvd/common_utils.h"

namespace cuttlefish {
namespace cvd_start_impl {

/* Picks up the line starting with [*] GUEST_BUILD_FINGERPRINT:,
 * and removes the "[*] GUEST_BUILD_FINGERPRINT:" part.
 *
 */
static Result<std::string> ExtractBuildIdLineValue(
    const std::string& home_dir) {
  std::string kernel_log_path =
      ConcatToString(home_dir, "/cuttlefish_runtime/kernel.log");
  std::ifstream kernel_log_file(kernel_log_path);
  CF_EXPECT(kernel_log_file.is_open(),
            "The " << kernel_log_path << " is not open.");
  std::regex pattern("\\[\\s*[0-9]*\\.[0-9]+\\]\\s*GUEST_BUILD_FINGERPRINT:");
  for (std::string line; std::getline(kernel_log_file, line);) {
    std::smatch matched;
    if (!std::regex_search(line, matched, pattern)) {
      continue;
    }
    return matched.suffix().str();
  }
  auto err_message =
      ConcatToString("The GUEST_BUILD_FINGERPRINT line is not found in the",
                     kernel_log_path, " file");
  return CF_ERR(err_message);
}

Result<std::string> ExtractBuildId(const std::string& home_dir) {
  auto fingerprint_line_value = CF_EXPECT(ExtractBuildIdLineValue(home_dir));
  /* format:
   *  <not sure>/target/build year/branch.id/who built it/when:target/??
   *
   * We need the branch followed by . followed by sort of Id part
   */
  std::vector<std::string> tokens =
      android::base::Tokenize(fingerprint_line_value, "/");
  CF_EXPECT(tokens.size() > 2);
  return tokens.at(3);
}

}  // namespace cvd_start_impl
}  // namespace cuttlefish
