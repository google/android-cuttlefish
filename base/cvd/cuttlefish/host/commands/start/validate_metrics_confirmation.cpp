//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/commands/start/validate_metrics_confirmation.h"

#include <iostream>
#include <string>

#include <android-base/macros.h>

#include "cuttlefish/host/libs/config/cuttlefish_config.h"

namespace cuttlefish {

std::string ValidateMetricsConfirmation(std::string use_metrics) {
  if (use_metrics.empty()) {
    if (CuttlefishConfig::ConfigExists()) {
      auto config = CuttlefishConfig::Get();
      if (config) {
        if (config->enable_metrics() == CuttlefishConfig::Answer::kYes) {
          use_metrics = "y";
        } else if (config->enable_metrics() == CuttlefishConfig::Answer::kNo) {
          use_metrics = "n";
        }
      }
    }
  }

  char ch = !use_metrics.empty() ? tolower(use_metrics.at(0)) : -1;
  if (ch != 'n') {
    if (use_metrics.empty()) {
      std::cout << "\nAutomatically send diagnostic information to Google from "
                   "this Android Virtual "
                   "Device. You can adjust this permission by running \"--report_anonymous_usage_stats=n\". (Y/n)?:";
    } else {
      std::cout << "You can adjust the permission for sending diagnostic "
                   "information to Google by running \"--report_anonymous_usage_stats=n\"\n";
    }
  }
  for (;;) {
    switch (ch) {
      case 0:
      case '\r':
      case '\n':
      case 'y':
        return "y";
      case 'n':
        return "n";
      default:
        std::cout << "Must accept/reject anonymous usage statistics reporting "
                     "(Y/n): ";
        FALLTHROUGH_INTENDED;
      case -1:
        std::cin.get(ch);
        // if there's no tty the EOF flag is set, in which case default to 'n'
        if (std::cin.eof()) {
          ch = 'n';
          std::cout << "n\n";  // for consistency with user input
        }
        ch = tolower(ch);
    }
  }
  return "";
}

}  // namespace cuttlefish
