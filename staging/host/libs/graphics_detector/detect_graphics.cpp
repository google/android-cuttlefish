/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include <string>

#include <gflags/gflags.h>

#include "android-base/logging.h"
#include "host/libs/graphics_detector/graphics_detector.h"

int main(int argc, char* argv[]) {
  ::android::base::InitLogging(argv, android::base::StdioLogger);
  ::android::base::SetMinimumLogSeverity(android::base::VERBOSE);
  ::gflags::ParseCommandLineFlags(&argc, &argv, true);
  LOG(INFO) << cuttlefish::GetGraphicsAvailabilityWithSubprocessCheck();
}