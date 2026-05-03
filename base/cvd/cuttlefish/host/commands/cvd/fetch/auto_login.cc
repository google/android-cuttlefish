//
// Copyright (C) 2026 The Android Open Source Project
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

#include "cuttlefish/host/commands/cvd/fetch/auto_login.h"

#include <string>
#include <string_view>

#include "cuttlefish/common/libs/utils/files.h"
#include "cuttlefish/common/libs/utils/is_corp.h"
#include "cuttlefish/common/libs/utils/subprocess.h"
#include "cuttlefish/common/libs/utils/subprocess_managed_stdio.h"
#include "cuttlefish/host/commands/cvd/fetch/build_api_credentials.h"
#include "cuttlefish/host/commands/cvd/fetch/build_api_flags.h"
#include "cuttlefish/host/libs/web/oauth2_consent.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr std::string_view kWrapperFilepath =
    "/google/src/head/depot/google3/cloud/android/login/login.sh";

bool IsCredentialFlagPresent(const BuildApiFlags& flags) {
  return !flags.credential_source.empty() ||
         flags.credential_flags.use_gce_metadata ||
         !flags.credential_flags.credential_filepath.empty() ||
         !flags.credential_flags.service_account_filepath.empty();
}

Result<bool> IsCredentialFilePresent() {
  return FileExists(GetAcloudOauthFilepath()) ||
         !CF_EXPECT(GetCvdCredentialFilepaths()).empty();
}

}  // namespace

Result<bool> ShouldAutoLogin(const BuildApiFlags& flags) {
  return !IsCredentialFlagPresent(flags) &&
         !CF_EXPECT(IsCredentialFilePresent(),
                    "Failed searching for credential files.") &&
         IsCorp();
}

// TODO CJR: better name
bool CanDetectScript() { return FileExists(std::string(kWrapperFilepath)); }

Result<void> RunAutoLogin() {
  Command login_command =
      Command(std::string(kWrapperFilepath)).AddParameter("--ssh");
  std::string stderr;
  const int exit_code =
      RunWithManagedStdio(std::move(login_command), nullptr, nullptr, &stderr);
  CF_EXPECTF(exit_code == 0, "Failure to automatically credential:\n{}",
             stderr);
  return {};
}

}  // namespace cuttlefish
