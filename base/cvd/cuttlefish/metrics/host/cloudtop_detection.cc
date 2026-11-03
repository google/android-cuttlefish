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

#include "cuttlefish/metrics/host/cloudtop_detection.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kBiosVendor = "/sys/class/dmi/id/bios_vendor";
constexpr std::string_view kProductSerial = "/sys/class/dmi/id/product_serial";
constexpr std::string_view kLsbRelease = "/etc/lsb-release";

constexpr std::string_view kExpectedVendor = "Google";
constexpr std::string_view kExpectedSerialPrefix = "GoogleCloud";
constexpr std::string_view kExpectedRoleKey = "GOOGLE_ROLE";
constexpr std::string_view kExpectedRole = "desktop";

Result<std::string> GetGoogleRole() {
  const std::string lsb_release =
      CF_EXPECT(ReadFileContents(std::string(kLsbRelease)));
  const std::vector<std::string> lines = absl::StrSplit(lsb_release, "\n");
  for (const std::string& line : lines) {
    const std::vector<std::string> parts = absl::StrSplit(line, "=");
    if (parts.size() == 2 && parts.front() == kExpectedRoleKey) {
      return parts.back();
    }
  }
  return "";
}

}  // namespace

Result<bool> IsCloudtop() {
  Result<std::string> bios_vendor = ReadFileContents(std::string(kBiosVendor));
  if (!bios_vendor) {
    return false;
  }

  Result<std::string> product_serial =
      ReadFileContents(std::string(kProductSerial));
  if (!product_serial) {
    return false;
  }

  Result<std::string> google_role = GetGoogleRole();
  return absl::StripAsciiWhitespace(*bios_vendor) == kExpectedVendor &&
         absl::StripAsciiWhitespace(*google_role) == kExpectedRole &&
         absl::StartsWith(*product_serial, kExpectedSerialPrefix);
}

}  // namespace cuttlefish
