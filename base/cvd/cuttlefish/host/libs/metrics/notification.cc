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

#include "cuttlefish/host/libs/metrics/notification.h"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "fmt/format.h"

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/host/libs/directories/xdg.h"
#include "cuttlefish/host/libs/metrics/enabled.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kNoticeFilename = "PRIVACY_NOTICE.txt";

constexpr std::string_view kTermsAndPrivacyNotice =
    "By using this Android Virtual Device, you agree to Google Terms of "
    "Service (https://policies.google.com/terms). The Google Privacy Policy "
    "(https://policies.google.com/privacy) describes how Google handles "
    "information generated as you use Google services.";

constexpr std::string_view kMetricsEnabledNotice =
    "This will automatically send diagnostic information to Google, such as "
    "crash reports and usage data from the host machine managing the Android "
    "Virtual Device.";

Result<std::string> GetNoticeFilepath() {
  return fmt::format("{}/{}", CF_EXPECT(CvdDataHome()), kNoticeFilename);
}

Result<void> WriteNoticeFile(const std::string& contents) {
  CF_EXPECT(WriteCvdDataFile(kNoticeFilename, contents));
  return {};
}

std::string GetNoticeMessage() {
  std::stringstream result;
  result << kTermsAndPrivacyNotice << std::endl;
  if (AreMetricsEnabled()) {
    result << kMetricsEnabledNotice << std::endl;
  }
  return result.str();
}

}  // namespace

void DisplayPrivacyNotice() {
  Result<std::string> filepath_result = GetNoticeFilepath();
  if (!filepath_result.ok()) {
    VLOG(0) << "Failed generating notice filepath: " << filepath_result.error();
  } else if (FileExists(*filepath_result)) {
    return;
  }

  const std::string notice_message = GetNoticeMessage();
  std::cout << notice_message;

  Result<void> write_result = WriteNoticeFile(notice_message);
  if (!write_result.ok()) {
    VLOG(0) << "Failed writing notice file: " << write_result.error();
  }
}

}  // namespace cuttlefish
