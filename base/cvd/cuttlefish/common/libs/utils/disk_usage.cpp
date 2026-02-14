/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "cuttlefish/common/libs/utils/disk_usage.h"

#include <stddef.h>

#include <string>
#include <utility>

#include <android-base/parseint.h>
#include <android-base/strings.h>

#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

static constexpr char kWhitespaceCharacters[] = " \n\t\r\v\f";

// return unit determined by the `--block-size` argument
Result<size_t> GetDiskUsage(const std::string& path,
                            const std::string& size_arg) {
  Command du_cmd("du");
  du_cmd.AddParameter("-s");  // summarize, only output total
  du_cmd.AddParameter(
      "--apparent-size");  // apparent size rather than device usage
  du_cmd.AddParameter("--block-size=" + size_arg);
  du_cmd.AddParameter(path);

  std::string out = CF_EXPECT(RunAndCaptureStdout(std::move(du_cmd)));
  std::vector<std::string> split_out =
      android::base::Tokenize(out, kWhitespaceCharacters);
  CF_EXPECTF(!split_out.empty(),
             "No valid output read from `du` command in \"{}\"", out);
  std::string total = split_out.front();

  size_t result;
  CF_EXPECTF(android::base::ParseUint(total, &result),
             "Failure parsing \"{}\" to integer.", total);
  return result;
}

}  // namespace

Result<size_t> GetDiskUsageBytes(const std::string& path) {
  return CF_EXPECTF(GetDiskUsage(path, "1"),
                    "Unable to determine disk usage of file \"{}\"", path);
}

Result<size_t> GetDiskUsageGigabytes(const std::string& path) {
  return CF_EXPECTF(GetDiskUsage(path, "1G"),
                    "Unable to determine disk usage of file \"{}\"", path);
}

}  // namespace cuttlefish
