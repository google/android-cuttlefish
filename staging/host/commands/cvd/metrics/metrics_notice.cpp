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

#include <iostream>
#include <map>
#include <string>

namespace cuttlefish {

namespace {

const std::string kContentLicensesUrl =
    "https://source.android.com/setup/start/licenses";
const std::map<std::string, std::string> kContributorAgreementUrl = {
    {"INTERNAL", "https://cla.developers.google.com/"},
    {"EXTERNAL", "https://opensource.google.com/docs/cla/"},
};
const std::string kPrivacyPolicyUrl = "https://policies.google.com/privacy";
const std::string kTermsServiceUrl = "https://policies.google.com/terms";

}  // namespace

// TODO (moelsherif@): Extend the function after supporting internal and
// external users.
std::string GetUserType() { return "INTERNAL"; }

void PrintDataCollectionNotice(bool colorful = true) {
  const std::string red = "31m";
  const std::string green = "32m";
  const std::string start = "\033[1;";
  const std::string end = "\033[0m";
  const std::string delimiter(18, '=');
  std::string anonymous;
  std::string user_type = "INTERNAL";

  if (GetUserType() == "EXTERNAL") {
    anonymous = " anonymous";
    user_type = "EXTERNAL";
  }

  std::string notice =
      "  We collect" + anonymous +
      " usage statistics in accordance with our Content "
      "Licenses (" +
      kContentLicensesUrl + "), Contributor License Agreement (" +
      kContributorAgreementUrl.at(user_type) +
      "), Privacy "
      "Policy (" +
      kPrivacyPolicyUrl + ") and Terms of Service (" + kTermsServiceUrl + ").";

  if (colorful) {
    std::cerr << "\n"
              << delimiter << "\n"
              << start << red << "Notice:" << end << std::endl;
    std::cerr << start << green << " " << notice << end << "\n"
              << delimiter << "\n";
  } else {
    std::cerr << "\n" << delimiter << "\nNotice:" << std::endl;
    std::cerr << " " << notice << "\n" << delimiter << "\n";
  }
}

}  // namespace cuttlefish
