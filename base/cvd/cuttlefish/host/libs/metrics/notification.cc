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
#include <string_view>

#include "cuttlefish/host/libs/metrics/enabled.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kTermsAndPrivacyNotice =
    "By using this Android Virtual Device, you agree to Google Terms of "
    "Service (https://policies.google.com/terms). The Google Privacy Policy "
    "(https://policies.google.com/privacy) describes how Google handles "
    "information generated as you use Google services.";

constexpr std::string_view kMetricsEnabledNotice =
    "This will automatically send diagnostic information to Google, such as "
    "crash reports and usage data from the host machine managing the Android "
    "Virtual Device.";

}  // namespace

void DisplayPrivacyNotice() {
  std::cout << kTermsAndPrivacyNotice << std::endl;
  if (AreMetricsEnabled()) {
    std::cout << kMetricsEnabledNotice << std::endl;
  }
}

}  // namespace cuttlefish
