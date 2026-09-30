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

#include "cuttlefish/host/commands/cvd/fetch/luci_credential_helper_source.h"

#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include "json/value.h"

#include "cuttlefish/common/libs/utils/environment.h"
#include "cuttlefish/common/libs/utils/json.h"
#include "cuttlefish/files/file_exists.h"
#include "cuttlefish/host/libs/web/credential_source.h"
#include "cuttlefish/process/command.h"
#include "cuttlefish/process/managed_stdio.h"
#include "cuttlefish/result/result.h"

namespace cuttlefish {
namespace {

constexpr char kLuciAuthCredentialHelperEnv[] = "LUCI_AUTH_CREDENTIAL_HELPER";
constexpr char kDefaultCredHelperBin[] = "/usr/bin/sso-cred-helper";
constexpr std::chrono::seconds kTokenTtl = std::chrono::hours(1);

}  // namespace

std::unique_ptr<CredentialSource> LuciCredentialHelperSource::Make(
    const std::string& scope) {
  std::string helper_bin =
      StringFromEnv(kLuciAuthCredentialHelperEnv, kDefaultCredHelperBin);
  if (helper_bin.empty() || !FileExists(helper_bin)) {
    return nullptr;
  }
  return std::unique_ptr<CredentialSource>(
      new LuciCredentialHelperSource(std::move(helper_bin), scope));
}

LuciCredentialHelperSource::LuciCredentialHelperSource(std::string helper_bin,
                                                       std::string scope)
    : helper_bin_(std::move(helper_bin)), scope_(std::move(scope)) {}

Result<std::pair<std::string, std::chrono::seconds>>
LuciCredentialHelperSource::Refresh() {
  CF_EXPECTF(FileExists(helper_bin_), "Could not find {}", helper_bin_);

  Command cmd = Command(helper_bin_).AddParameter("--scopes=" + scope_);
  std::string stdout_str;
  int exit_code =
      RunWithManagedStdio(std::move(cmd), nullptr, &stdout_str, nullptr);
  CF_EXPECTF(exit_code == 0, "Failed to execute `{}`, exit code: {}",
             helper_bin_, exit_code);

  Json::Value json = CF_EXPECT(ParseJson(stdout_str));
  CF_EXPECT(json.isMember("token") && json["token"].isString(),
            "Missing or invalid 'token' field in credential helper output");
  std::string token = json["token"].asString();
  CF_EXPECT(!token.empty(), "Empty 'token' in credential helper output");

  return std::make_pair(std::move(token), kTokenTtl);
}

}  // namespace cuttlefish
