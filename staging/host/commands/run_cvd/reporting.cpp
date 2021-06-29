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

#include "host/commands/run_cvd/reporting.h"

#include <android-base/logging.h>
#include <fruit/fruit.h>
#include <string>
#include <vector>

namespace cuttlefish {

static constexpr char kGreenColor[] = "\033[1;32m";
static constexpr char kResetColor[] = "\033[0m";

DiagnosticInformation::~DiagnosticInformation() = default;

void DiagnosticInformation::PrintAll(
    const std::vector<DiagnosticInformation*>& infos) {
  LOG(INFO) << kGreenColor
            << "The following files contain useful debugging information:"
            << kResetColor;
  for (const auto& info : infos) {
    for (const auto& line : info->Diagnostics()) {
      LOG(INFO) << kGreenColor << "  " << line << kResetColor;
    }
  }
}

}  // namespace cuttlefish
