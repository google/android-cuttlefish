/*
 * Copyright (C) 2024 The Android Open Source Project
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
#include "host/libs/config/secure_hals.h"

#include <cctype>
#include <set>
#include <string>
#include <unordered_map>

#include <android-base/no_destructor.h>
#include <android-base/strings.h>

#include "common/libs/utils/result.h"

using android::base::NoDestructor;
using android::base::Tokenize;

namespace cuttlefish {
namespace {

NoDestructor<std::unordered_map<std::string_view, SecureHal>> kMapping([] {
  return std::unordered_map<std::string_view, SecureHal>{
      {"keymint", SecureHal::kHostKeymintSecure},
      {"host_secure_keymint", SecureHal::kHostKeymintSecure},
      {"host_keymint_secure", SecureHal::kHostKeymintSecure},
      {"guest_gatekeeper_insecure", SecureHal::kGuestGatekeeperInsecure},
      {"guest_insecure_gatekeeper", SecureHal::kGuestGatekeeperInsecure},
      {"guest_insecure_keymint", SecureHal::kGuestKeymintInsecure},
      {"guest_keymint_insecure", SecureHal::kGuestKeymintInsecure},
      {"gatekeeper", SecureHal::kHostGatekeeperSecure},
      {"host_gatekeeper_secure", SecureHal::kHostGatekeeperSecure},
      {"host_secure_gatekeeper", SecureHal::kHostGatekeeperSecure},
      {"host_gatekeeper_insecure", SecureHal::kHostGatekeeperInsecure},
      {"host_insecure_gatekeeper", SecureHal::kHostGatekeeperInsecure},
      {"oemlock", SecureHal::kHostOemlockSecure},
      {"host_oemlock_secure", SecureHal::kHostOemlockSecure},
      {"host_secure_oemlock", SecureHal::kHostOemlockSecure},
  };
}());

}  // namespace

Result<SecureHal> ParseSecureHal(std::string mode) {
  for (char& c : mode) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  auto it = kMapping->find(mode);
  CF_EXPECTF(it != kMapping->end(), "Unknown secure HAL '{}'", mode);
  return it->second;
}

Result<std::set<SecureHal>> ParseSecureHals(const std::string& hals) {
  std::set<SecureHal> args_set;
  for (auto& hal : Tokenize(hals, ",:;|/\\+")) {
    args_set.emplace(CF_EXPECT(ParseSecureHal(hal)));
  }
  return args_set;
}

Result<void> ValidateSecureHals(const std::set<SecureHal>& secure_hals) {
  auto keymint_impls = secure_hals.count(SecureHal::kGuestKeymintInsecure) +
                       secure_hals.count(SecureHal::kHostKeymintInsecure) +
                       secure_hals.count(SecureHal::kHostKeymintSecure);
  CF_EXPECT_LE(keymint_impls, 1, "Choose at most one keymint implementation");

  auto gatekeeper_impls =
      secure_hals.count(SecureHal::kGuestGatekeeperInsecure) +
      secure_hals.count(SecureHal::kHostGatekeeperInsecure) +
      secure_hals.count(SecureHal::kHostGatekeeperSecure);
  CF_EXPECT_LE(gatekeeper_impls, 1,
               "Choose at most one gatekeeper implementation");

  auto oemlock_impls = secure_hals.count(SecureHal::kHostOemlockInsecure) +
                       secure_hals.count(SecureHal::kHostOemlockSecure);
  CF_EXPECT_LE(oemlock_impls, 1, "Choose at most one oemlock implementation");

  return {};
}

std::string ToString(SecureHal hal_in) {
  switch (hal_in) {
    case SecureHal::kGuestGatekeeperInsecure:
      return "guest_gatekeeper_insecure";
    case SecureHal::kGuestKeymintInsecure:
      return "guest_keymint_insecure";
    case SecureHal::kHostKeymintInsecure:
      return "host_keymint_insecure";
    case SecureHal::kHostKeymintSecure:
      return "host_keymint_secure";
    case SecureHal::kHostGatekeeperInsecure:
      return "host_gatekeeper_insecure";
    case SecureHal::kHostGatekeeperSecure:
      return "host_gatekeeper_secure";
    case SecureHal::kHostOemlockInsecure:
      return "host_oemlock_insecure";
    case SecureHal::kHostOemlockSecure:
      return "host_oemlock_secure";
  }
}

}  // namespace cuttlefish
